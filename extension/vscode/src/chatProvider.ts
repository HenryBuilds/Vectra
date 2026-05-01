// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Chat panel for Vectra. Hosted as a singleton WebviewPanel opened
// with ViewColumn.Beside so it lives as an editor tab next to the
// code — same UX as Anthropic's Claude Code extension — instead of
// being squeezed into the auxiliary sidebar.
//
// Communication contract (postMessage):
//
//   webview -> extension
//     { type: 'send',      text, model, effort, topK }
//     { type: 'newChat' }                    // host clears its
//                                            // own state if any
//     { type: 'cancel' }                     // kill running proc
//     { type: 'ready' }                      // initial handshake
//
//   extension -> webview
//     { type: 'chunk',     id, text }        // streamed stdout
//     { type: 'meta',      id, text }        // line off stderr
//     { type: 'done',      id, exitCode }
//     { type: 'error',     id, message }
//     { type: 'started',   id }              // assistant turn begins
//     { type: 'config',    binary, models }  // initial defaults
//
// Each turn carries a stable `id` so the webview can route streamed
// chunks to the right assistant bubble even if the user starts a
// follow-up before the previous turn finishes.

import * as cp from 'child_process';
import * as crypto from 'crypto';
import * as vscode from 'vscode';

interface SendMessage {
    type: 'send';
    id: string;
    text: string;
    model?: string;
    effort?: string;
    topK?: number;
}

interface CancelMessage {
    type: 'cancel';
    id: string;
}

interface NewChatMessage {
    type: 'newChat';
}

interface ReadyMessage {
    type: 'ready';
}

interface ActionMessage {
    type: 'action';
    commandId: string;
}

interface OpenFileMessage {
    type: 'openFile';
    path: string;
    line: number;
}

interface MessageAction {
    label: string;
    commandId: string;
}

type WebviewMessage =
    | SendMessage
    | CancelMessage
    | NewChatMessage
    | ReadyMessage
    | ActionMessage
    | OpenFileMessage;

export class VectraChatPanel {
    // viewType doubles as the `activeWebviewPanelId` context key value
    // VS Code exposes for menu `when` clauses.
    public static readonly viewType = 'vectra.chat';

    private static current: VectraChatPanel | undefined;

    private readonly panel: vscode.WebviewPanel;
    private readonly extensionUri: vscode.Uri;
    private readonly disposables: vscode.Disposable[] = [];
    private readonly active = new Map<string, cp.ChildProcess>();

    // Conversation continuity. One UUID per chat session (one panel
    // = one session); reset on newChat. claude -p is one-shot per
    // invocation, but it persists transcripts to disk under whatever
    // `--session-id` it was given, so passing the same UUID on every
    // turn — first as --session-id, then as --resume — gives the
    // user a real multi-turn conversation instead of N independent
    // monologues.
    private sessionId: string = crypto.randomUUID();
    private hasSentTurn: boolean = false;

    public static createOrShow(extensionUri: vscode.Uri): void {
        const column = vscode.ViewColumn.Beside;

        if (VectraChatPanel.current) {
            VectraChatPanel.current.panel.reveal(column, false);
            return;
        }

        const panel = vscode.window.createWebviewPanel(
            VectraChatPanel.viewType,
            'Vectra',
            { viewColumn: column, preserveFocus: false },
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [
                    vscode.Uri.joinPath(extensionUri, 'media'),
                    vscode.Uri.joinPath(extensionUri, 'out'),
                ],
            },
        );

        const iconUri = vscode.Uri.joinPath(extensionUri, 'media', 'icon.svg');
        panel.iconPath = { light: iconUri, dark: iconUri };

        VectraChatPanel.current = new VectraChatPanel(panel, extensionUri);
    }

    public static newChat(): void {
        VectraChatPanel.current?.newChatInternal();
    }

    private constructor(panel: vscode.WebviewPanel, extensionUri: vscode.Uri) {
        this.panel = panel;
        this.extensionUri = extensionUri;

        this.panel.webview.html = this.renderHtml(this.panel.webview);

        this.panel.webview.onDidReceiveMessage(
            (m: WebviewMessage) => this.handleMessage(m),
            null,
            this.disposables,
        );

        this.panel.onDidDispose(() => this.dispose(), null, this.disposables);
    }

    private dispose(): void {
        VectraChatPanel.current = undefined;
        this.killAll();
        while (this.disposables.length > 0) {
            this.disposables.pop()?.dispose();
        }
    }

    private newChatInternal(): void {
        this.panel.webview.postMessage({ type: 'newChat' });
        this.killAll();
        // Fresh UUID for the next conversation — never reuse a
        // session id across "+ New chat", or claude would happily
        // reload the previous transcript.
        this.sessionId = crypto.randomUUID();
        this.hasSentTurn = false;
    }

    private handleMessage(m: WebviewMessage): void {
        switch (m.type) {
            case 'send':
                this.handleSend(m);
                break;
            case 'cancel':
                this.handleCancel(m);
                break;
            case 'newChat':
                // Kill anything in flight; the webview owns the
                // visual reset.
                this.killAll();
                break;
            case 'ready':
                this.postConfig();
                break;
            case 'action':
                // Action buttons rendered inside chat bubbles
                // dispatch back to a registered VS Code command.
                void vscode.commands.executeCommand(m.commandId);
                break;
            case 'openFile':
                // Source citations clicked in the assistant bubble.
                // The path is repo-relative; resolve against the
                // active workspace folder.
                void this.openSource(m.path, m.line);
                break;
        }
    }

    private postConfig(): void {
        const cfg = vscode.workspace.getConfiguration('vectra');
        this.post({
            type: 'config',
            binary: cfg.get<string>('binary', 'vectra'),
            defaultModel: cfg.get<string>('claudeModel', ''),
            defaultEffort: cfg.get<string>('effort', ''),
            defaultTopK: cfg.get<number>('topK', 0),
        });
    }

    private workspaceRoot(): string | undefined {
        return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
    }

    private handleSend(m: SendMessage): void {
        const cwd = this.workspaceRoot();
        if (!cwd) {
            this.post({
                type: 'error',
                id: m.id,
                message: 'Vectra needs an open workspace folder.',
            });
            this.post({ type: 'done', id: m.id, exitCode: -1 });
            return;
        }

        const cfg = vscode.workspace.getConfiguration('vectra');
        const binary = (cfg.get<string>('binary', 'vectra').trim() || 'vectra');

        const args: string[] = ['ask', m.text];
        if (m.topK && m.topK > 0) {
            args.push('-k', String(m.topK));
        }
        if (m.model && m.model.length > 0) {
            args.push('--claude-model', m.model);
        }
        if (m.effort && m.effort.length > 0) {
            args.push('--effort', m.effort);
        }
        // First turn assigns the session id; every subsequent turn
        // resumes it so claude sees the prior transcript on disk.
        if (this.hasSentTurn) {
            args.push('--resume', this.sessionId);
        } else {
            args.push('--session-id', this.sessionId);
            this.hasSentTurn = true;
        }
        // Always opt in to stream-json. The webview renderer expects
        // newline-delimited claude events; the legacy text path is a
        // fallback for lines that fail JSON.parse (older claude
        // binaries, error output before claude starts).
        args.push('--stream-json');

        this.post({ type: 'started', id: m.id });

        let proc: cp.ChildProcess;
        try {
            proc = cp.spawn(binary, args, { cwd, env: process.env });
        } catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            this.post({
                type: 'error',
                id: m.id,
                message: `Vectra failed to spawn '${binary}': ${msg}. ` +
                    `Set 'vectra.binary' in settings to an absolute path.`,
            });
            this.post({ type: 'done', id: m.id, exitCode: -1 });
            return;
        }

        this.active.set(m.id, proc);

        let stderrBuffer = '';
        // Buffer for newline-delimited JSON. claude can split a
        // single event across multiple data chunks (especially with
        // --include-partial-messages emitting hundreds of tiny
        // text_deltas), so we accumulate until we see a `\n`.
        let stdoutBuffer = '';

        proc.stdout?.on('data', (d: Buffer) => {
            stdoutBuffer += d.toString('utf8');
            let nl = stdoutBuffer.indexOf('\n');
            while (nl !== -1) {
                const line = stdoutBuffer.slice(0, nl);
                stdoutBuffer = stdoutBuffer.slice(nl + 1);
                this.dispatchClaudeLine(m.id, line);
                nl = stdoutBuffer.indexOf('\n');
            }
        });
        proc.stderr?.on('data', (d: Buffer) => {
            // Vectra prints retrieval pipeline timings + status to
            // stderr; surface those as meta lines so the user sees
            // what's happening before claude's reply arrives.
            const chunk = d.toString('utf8');
            stderrBuffer += chunk;
            this.post({ type: 'meta', id: m.id, text: chunk });
        });
        proc.on('error', (err) => {
            this.post({ type: 'error', id: m.id, message: err.message });
        });
        proc.on('close', (code) => {
            // Flush any trailing data without a final newline.
            if (stdoutBuffer.length > 0) {
                this.dispatchClaudeLine(m.id, stdoutBuffer);
                stdoutBuffer = '';
            }
            this.active.delete(m.id);
            const exit = code ?? 0;

            // Pattern-match common Vectra errors and surface a
            // structured error bubble with actionable buttons. The
            // raw stderr already streamed as meta; this gives the
            // user a single, clearly-rendered next step.
            if (exit !== 0) {
                const actions = this.actionsForStderr(stderrBuffer);
                const friendly = this.friendlyMessage(stderrBuffer, exit);
                this.post({
                    type: 'error',
                    id: m.id,
                    message: friendly,
                    actions: actions.length > 0 ? actions : undefined,
                });
            }
            this.post({ type: 'done', id: m.id, exitCode: exit });
        });
    }

    // One line off claude's stdout. With --output-format=stream-json
    // each line is a JSON object documented at:
    //   https://docs.anthropic.com/en/docs/agents-and-tools/claude-code/sdk
    // and shaped roughly like the Anthropic SDK streaming events,
    // plus a few claude-specific top-level types (system / user /
    // result). Anything that fails JSON.parse is forwarded as a
    // legacy text chunk so a broken/older claude binary still
    // produces visible output instead of silence.
    private dispatchClaudeLine(id: string, raw: string): void {
        const line = raw.trim();
        if (!line) {
            return;
        }
        let event: unknown;
        try {
            event = JSON.parse(line);
        } catch {
            this.post({ type: 'chunk', id, text: raw + '\n' });
            return;
        }
        this.handleClaudeEvent(id, event);
    }

    private handleClaudeEvent(id: string, event: unknown): void {
        if (!event || typeof event !== 'object') {
            return;
        }
        const e = event as { type?: string; [k: string]: unknown };
        switch (e.type) {
            case 'system':
                // Init info (session_id, model, cwd, …). The chat
                // panel does not need any of it today.
                return;
            case 'stream_event':
                this.handleStreamEvent(id, e['event']);
                return;
            case 'assistant':
                // Full assistant message at end. We already
                // accumulated each block via stream_event deltas, so
                // re-rendering from the canonical message would
                // duplicate content.
                return;
            case 'user':
                this.handleUserMessage(id, e['message']);
                return;
            case 'result':
                this.handleResult(id, e);
                return;
            case 'vectra_event':
                // Vectra-emitted side-channel events on the same
                // NDJSON stream. Currently only "context", which
                // carries the retrieved chunks as a list — the
                // webview renders them as a clickable Sources
                // footer.
                if (e['subtype'] === 'context') {
                    this.handleContextEvent(id, e['chunks']);
                }
                return;
        }
    }

    private handleContextEvent(id: string, chunks: unknown): void {
        if (!Array.isArray(chunks)) {
            return;
        }
        const sources: Array<{
            file: string;
            startLine: number;
            endLine: number;
            symbol: string;
            kind: string;
        }> = [];
        for (const c of chunks) {
            if (!c || typeof c !== 'object') continue;
            const obj = c as {
                file?: unknown;
                start_line?: unknown;
                end_line?: unknown;
                symbol?: unknown;
                kind?: unknown;
            };
            if (typeof obj.file !== 'string') continue;
            sources.push({
                file: obj.file,
                startLine: typeof obj.start_line === 'number' ? obj.start_line : 0,
                endLine: typeof obj.end_line === 'number' ? obj.end_line : 0,
                symbol: typeof obj.symbol === 'string' ? obj.symbol : '',
                kind: typeof obj.kind === 'string' ? obj.kind : '',
            });
        }
        this.post({ type: 'sources', id, sources });
    }

    // Resolve a repo-relative path against the active workspace
    // folder and reveal it at `line` (1-indexed). Used by the
    // Sources footer when the user clicks a citation.
    private async openSource(relPath: string, line: number): Promise<void> {
        const folder = vscode.workspace.workspaceFolders?.[0];
        if (!folder) {
            void vscode.window.showErrorMessage('Vectra: no workspace folder open.');
            return;
        }
        const uri = vscode.Uri.joinPath(folder.uri, relPath);
        try {
            const doc = await vscode.workspace.openTextDocument(uri);
            const lineIdx = Math.max(0, line - 1);
            const range = new vscode.Range(lineIdx, 0, lineIdx, 0);
            await vscode.window.showTextDocument(doc, {
                selection: range,
                preview: false,
                preserveFocus: false,
            });
        } catch (err) {
            const msg = err instanceof Error ? err.message : String(err);
            void vscode.window.showErrorMessage(
                `Vectra: could not open ${relPath}: ${msg}`,
            );
        }
    }

    private handleStreamEvent(id: string, evt: unknown): void {
        if (!evt || typeof evt !== 'object') {
            return;
        }
        const e = evt as { type?: string; index?: number; [k: string]: unknown };

        switch (e.type) {
            case 'content_block_start': {
                const idx = e['index'];
                const block = e['content_block'] as { type?: string; id?: string; name?: string } | undefined;
                if (typeof idx !== 'number' || !block) {
                    return;
                }
                if (block.type === 'text') {
                    this.post({ type: 'block_start', id, blockIndex: idx, block: { kind: 'text' } });
                } else if (block.type === 'tool_use') {
                    this.post({
                        type: 'block_start',
                        id,
                        blockIndex: idx,
                        block: {
                            kind: 'tool_use',
                            toolUseId: block.id ?? '',
                            name: block.name ?? '<unknown>',
                        },
                    });
                } else if (block.type === 'thinking') {
                    this.post({ type: 'block_start', id, blockIndex: idx, block: { kind: 'thinking' } });
                }
                return;
            }
            case 'content_block_delta': {
                const idx = e['index'];
                const delta = e['delta'] as
                    | { type?: string; text?: string; partial_json?: string; thinking?: string }
                    | undefined;
                if (typeof idx !== 'number' || !delta) {
                    return;
                }
                if (delta.type === 'text_delta' && typeof delta.text === 'string') {
                    this.post({
                        type: 'block_delta',
                        id,
                        blockIndex: idx,
                        delta: { kind: 'text', text: delta.text },
                    });
                } else if (delta.type === 'input_json_delta' && typeof delta.partial_json === 'string') {
                    this.post({
                        type: 'block_delta',
                        id,
                        blockIndex: idx,
                        delta: { kind: 'tool_input', partialJson: delta.partial_json },
                    });
                } else if (delta.type === 'thinking_delta' && typeof delta.thinking === 'string') {
                    this.post({
                        type: 'block_delta',
                        id,
                        blockIndex: idx,
                        delta: { kind: 'thinking', text: delta.thinking },
                    });
                }
                return;
            }
            case 'content_block_stop': {
                const idx = e['index'];
                if (typeof idx === 'number') {
                    this.post({ type: 'block_stop', id, blockIndex: idx });
                }
                return;
            }
            // message_start / message_delta / message_stop carry
            // overall metadata; the canonical totals arrive in the
            // top-level 'result' event so we ignore these.
        }
    }

    private handleUserMessage(id: string, message: unknown): void {
        // claude returns tool_result blocks as an injected user
        // message. We attach them to the matching tool_use block on
        // the active assistant turn.
        if (!message || typeof message !== 'object') {
            return;
        }
        const content = (message as { content?: unknown }).content;
        if (!Array.isArray(content)) {
            return;
        }
        for (const block of content) {
            if (!block || typeof block !== 'object') continue;
            const b = block as {
                type?: string;
                tool_use_id?: string;
                content?: unknown;
                is_error?: boolean;
            };
            if (b.type !== 'tool_result' || !b.tool_use_id) {
                continue;
            }
            const text =
                typeof b.content === 'string'
                    ? b.content
                    : Array.isArray(b.content)
                      ? b.content
                            .map((c) =>
                                typeof c === 'object' && c && 'text' in c
                                    ? String((c as { text: unknown }).text ?? '')
                                    : JSON.stringify(c),
                            )
                            .join('\n')
                      : JSON.stringify(b.content);
            this.post({
                type: 'tool_result',
                id,
                toolUseId: b.tool_use_id,
                content: text,
                isError: b.is_error === true,
            });
        }
    }

    private handleResult(id: string, event: { [k: string]: unknown }): void {
        const usageRaw = (event['usage'] ?? {}) as {
            input_tokens?: number;
            output_tokens?: number;
            cache_read_input_tokens?: number;
            cache_creation_input_tokens?: number;
        };
        this.post({
            type: 'usage',
            id,
            usage: {
                inputTokens: usageRaw.input_tokens,
                outputTokens: usageRaw.output_tokens,
                cacheReadInputTokens: usageRaw.cache_read_input_tokens,
                cacheCreationInputTokens: usageRaw.cache_creation_input_tokens,
                costUsd: typeof event['total_cost_usd'] === 'number' ? (event['total_cost_usd'] as number) : undefined,
                durationMs: typeof event['duration_ms'] === 'number' ? (event['duration_ms'] as number) : undefined,
            },
        });
    }

    private actionsForStderr(stderr: string): MessageAction[] {
        if (/error:\s*index not found at/i.test(stderr)) {
            return [{ label: 'Index this workspace', commandId: 'vectra.index' }];
        }
        if (/error:\s*no project root detected/i.test(stderr)) {
            return [{ label: 'Index this workspace', commandId: 'vectra.index' }];
        }
        if (/error:\s*model not cached/i.test(stderr)) {
            // No first-class command for `vectra model pull` yet;
            // surface the manual hint without an action button.
            return [];
        }
        return [];
    }

    private friendlyMessage(stderr: string, exit: number): string {
        const indexMatch = stderr.match(/error:\s*index not found at\s+([^\n]+)/i);
        if (indexMatch) {
            return `No Vectra index at ${indexMatch[1].trim()}. ` +
                `Run "Vectra: Index workspace" first, then resend.`;
        }
        if (/error:\s*no project root detected/i.test(stderr)) {
            return 'No project root detected (looked for .vectra or .git). ' +
                'Open a project folder, or initialise the index.';
        }
        if (/error:\s*model not cached/i.test(stderr)) {
            return 'Embedding model is not cached locally. ' +
                'Run `vectra model pull <name>` in a terminal, then retry.';
        }
        if (/error:\s*unknown model/i.test(stderr)) {
            return 'Unknown embedding model. Run `vectra model list` to see available names.';
        }
        // Fallback: surface the last `error:` line if any, else the
        // exit code, so the user sees something concrete.
        const errorLine = stderr.match(/error:[^\n]+/i);
        if (errorLine) {
            return errorLine[0];
        }
        return `vectra exited with code ${exit}.`;
    }

    private handleCancel(m: CancelMessage): void {
        const proc = this.active.get(m.id);
        if (proc) {
            proc.kill();
            this.active.delete(m.id);
        }
    }

    private killAll(): void {
        for (const proc of this.active.values()) {
            proc.kill();
        }
        this.active.clear();
    }

    private post(message: unknown): void {
        this.panel.webview.postMessage(message);
    }

    private renderHtml(webview: vscode.Webview): string {
        // The React tree is bundled by esbuild into out/webview.js;
        // CSS stays a separate file so theme tweaks don't require
        // re-bundling JS.
        const cssUri = webview.asWebviewUri(
            vscode.Uri.joinPath(this.extensionUri, 'media', 'chat.css'),
        );
        const jsUri = webview.asWebviewUri(
            vscode.Uri.joinPath(this.extensionUri, 'out', 'webview.js'),
        );
        const nonce = randomNonce();

        return /* html */ `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8" />
  <meta http-equiv="Content-Security-Policy"
        content="default-src 'none';
                 style-src ${webview.cspSource} 'unsafe-inline';
                 script-src 'nonce-${nonce}';
                 img-src ${webview.cspSource} data:;
                 font-src ${webview.cspSource};">
  <link rel="stylesheet" href="${cssUri}" />
  <title>Vectra Chat</title>
</head>
<body>
  <div id="root"></div>
  <script nonce="${nonce}" src="${jsUri}"></script>
</body>
</html>`;
    }
}

function randomNonce(): string {
    const chars = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789';
    let out = '';
    for (let i = 0; i < 32; ++i) {
        out += chars[Math.floor(Math.random() * chars.length)];
    }
    return out;
}
