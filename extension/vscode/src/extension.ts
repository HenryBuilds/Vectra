// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Vectra VS Code extension — thin shell-out around the `vectra` CLI.
//
// Three commands:
//   vectra.ask                 InputBox -> `vectra ask "<task>"`
//   vectra.askAboutSelection   Wraps the editor's selection in the
//                              prompt and dispatches.
//   vectra.index               `vectra index .` on the workspace root.
//
// All output streams into a dedicated Output Channel ("Vectra"). The
// extension does no editing of its own — Claude Code, spawned by the
// CLI, owns the file-modification side. We just route input/output.

import * as cp from 'child_process';
import * as vscode from 'vscode';

import { AutoIndexer } from './autoIndexer';
import { ChatStorage } from './chatStorage';
import { VectraChatPanel } from './chatProvider';
import { indexLock } from './indexLock';
import { PermissionBridge } from './permissionBridge';

const OUTPUT_CHANNEL_NAME = 'Vectra';

interface AskFlags {
    claudeModel?: string;
    effort?: string;
    topK?: number;
    permissionMode?: string;
    indexModel?: string;
    reranker?: string;
}

function readSettings(): { binary: string; flags: AskFlags } {
    const config = vscode.workspace.getConfiguration('vectra');
    const binary = config.get<string>('binary', 'vectra').trim() || 'vectra';
    const claudeModel = config.get<string>('claudeModel', '').trim();
    const effort = config.get<string>('effort', '').trim();
    const topK = config.get<number>('topK', 0);
    const permissionMode = config.get<string>('permissionMode', '').trim();
    const indexModel = config.get<string>('indexModel', '').trim();
    const reranker = config.get<string>('reranker', '').trim();
    return {
        binary,
        flags: {
            claudeModel: claudeModel || undefined,
            effort: effort || undefined,
            topK: topK > 0 ? topK : undefined,
            permissionMode: permissionMode || undefined,
            indexModel: indexModel || undefined,
            reranker: reranker || undefined,
        },
    };
}

function workspaceRoot(): string | undefined {
    return vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
}

function quoteArg(a: string): string {
    return /[\s"]/.test(a) ? `"${a.replace(/"/g, '\\"')}"` : a;
}

/**
 * Spawn the vectra CLI, stream stdout+stderr into the output channel,
 * and resolve once the child exits. The promise resolves to the exit
 * code. Cancelling the user-facing progress notification kills the
 * subprocess.
 */
function runVectra(
    args: string[],
    cwd: string,
    output: vscode.OutputChannel,
    progressTitle: string,
): Thenable<number> {
    const { binary } = readSettings();

    output.show(true);
    output.appendLine(`> ${[binary, ...args].map(quoteArg).join(' ')}`);
    output.appendLine('');

    return vscode.window.withProgress(
        {
            location: vscode.ProgressLocation.Notification,
            title: progressTitle,
            cancellable: true,
        },
        (_progress, token) =>
            new Promise<number>((resolve) => {
                let proc: cp.ChildProcess;
                try {
                    proc = cp.spawn(binary, args, { cwd, env: process.env });
                } catch (err) {
                    const msg = err instanceof Error ? err.message : String(err);
                    output.appendLine(`\nerror: spawn failed: ${msg}`);
                    vscode.window.showErrorMessage(
                        `Vectra failed to spawn '${binary}': ${msg}. ` +
                            `Set 'vectra.binary' in settings to an absolute path.`,
                    );
                    resolve(-1);
                    return;
                }

                token.onCancellationRequested(() => {
                    proc.kill();
                    output.appendLine('\n[cancelled]');
                });

                proc.stdout?.on('data', (d: Buffer) => output.append(d.toString('utf8')));
                proc.stderr?.on('data', (d: Buffer) => output.append(d.toString('utf8')));

                proc.on('error', (err) => {
                    output.appendLine(`\nerror: ${err.message}`);
                    vscode.window.showErrorMessage(
                        `Vectra failed: ${err.message}. ` +
                            `Verify '${binary}' is on PATH or set 'vectra.binary' to an absolute path.`,
                    );
                    resolve(-1);
                });

                proc.on('close', (code) => {
                    output.appendLine(`\n[vectra exited with code ${code ?? 0}]`);
                    resolve(code ?? 0);
                });
            }),
    );
}

function buildAskArgs(task: string, flags: AskFlags): string[] {
    const args = ['ask', task];
    if (flags.topK !== undefined) {
        args.push('-k', String(flags.topK));
    }
    // The retrieval-side knobs go before the claude-side knobs so
    // `vectra ask --help` examples read naturally; argparse order
    // doesn't matter in practice.
    if (flags.indexModel) {
        args.push('--model', flags.indexModel);
    }
    if (flags.reranker) {
        args.push('--reranker', flags.reranker);
    }
    if (flags.claudeModel) {
        args.push('--claude-model', flags.claudeModel);
    }
    if (flags.effort) {
        args.push('--effort', flags.effort);
    }
    if (flags.permissionMode) {
        args.push('--permission-mode', flags.permissionMode);
    }
    return args;
}

async function commandAsk(output: vscode.OutputChannel): Promise<void> {
    const cwd = workspaceRoot();
    if (!cwd) {
        vscode.window.showErrorMessage('Vectra needs an open workspace folder.');
        return;
    }

    const task = await vscode.window.showInputBox({
        prompt: 'What should Vectra ask Claude Code?',
        placeHolder: 'e.g. "rename Foo to Bar in src/" or "wo wird auth gemacht"',
        ignoreFocusOut: true,
    });
    if (!task) {
        return;
    }

    const { flags } = readSettings();
    await runVectra(buildAskArgs(task, flags), cwd, output, 'Vectra is asking Claude Code…');
}

async function commandAskAboutSelection(output: vscode.OutputChannel): Promise<void> {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.selection.isEmpty) {
        vscode.window.showErrorMessage('Vectra: select some code first.');
        return;
    }

    const cwd = workspaceRoot();
    if (!cwd) {
        vscode.window.showErrorMessage('Vectra needs an open workspace folder.');
        return;
    }

    const selection = editor.document.getText(editor.selection);
    const fileLabel = vscode.workspace.asRelativePath(editor.document.uri);
    const startLine = editor.selection.start.line + 1;
    const endLine = editor.selection.end.line + 1;

    const intent = await vscode.window.showInputBox({
        prompt: `Vectra: what should Claude Code do with ${fileLabel}:${startLine}-${endLine}?`,
        placeHolder: 'e.g. "explain", "add a unit test", "refactor to async"',
        ignoreFocusOut: true,
        value: 'Explain this code:',
    });
    if (!intent) {
        return;
    }

    const task = `${intent}\n\nFrom ${fileLabel}:${startLine}-${endLine}\n\n\`\`\`\n${selection}\n\`\`\``;
    const { flags } = readSettings();
    await runVectra(buildAskArgs(task, flags), cwd, output, 'Vectra is asking Claude Code…');
}

// Build the argv for `vectra index .`. Pulls the active embedding
// model setting so manual + auto + reindex-with-model all stay in
// sync.
function buildIndexArgs(): string[] {
    const args = ['index', '.'];
    const indexModel = vscode.workspace
        .getConfiguration('vectra')
        .get<string>('indexModel', '')
        .trim();
    if (indexModel.length > 0) {
        args.push('--model', indexModel);
    }
    return args;
}

async function commandIndex(output: vscode.OutputChannel): Promise<void> {
    const cwd = workspaceRoot();
    if (!cwd) {
        vscode.window.showErrorMessage('Vectra needs an open workspace folder.');
        return;
    }
    // Funnel through indexLock so a manual click does not race with the
    // auto-indexer's FileSystemWatcher. A short wait when the auto path
    // is mid-run is the right tradeoff — concurrent writers on the same
    // .vectra/ SQLite file would otherwise stall on its internal lock.
    if (indexLock.busy) {
        output.appendLine(
            '[vectra: waiting for the auto-indexer to finish before running manual index…]',
        );
    }
    const release = await indexLock.acquire();
    try {
        await runVectra(buildIndexArgs(), cwd, output, 'Vectra is indexing the workspace…');
    } finally {
        release();
    }
}

// Embedding-model picker. Shows the known model entries plus a
// "Symbol-only" option, persists the user's pick to
// vectra.indexModel (Workspace scope), and triggers a fresh
// re-index against the new model. The Workspace scope is
// deliberate: index choice is per-project (a small Cursor-style
// repo wants 0.6b, a heavy monorepo might want 4b), and a
// User-scope default would lock everyone to the same model.
async function commandReindexWithModel(output: vscode.OutputChannel): Promise<void> {
    const cwd = workspaceRoot();
    if (!cwd) {
        vscode.window.showErrorMessage('Vectra needs an open workspace folder.');
        return;
    }

    type Item = vscode.QuickPickItem & { value: string };
    const items: Item[] = [
        {
            label: 'Symbol-only',
            description: 'no embeddings, FTS5 + tree-sitter symbols only',
            detail: 'Smallest DB. No GPU / model download required.',
            value: '',
        },
        {
            label: 'qwen3-embed-0.6b',
            description: '1024-dim · ~600 MB · CPU-friendly',
            detail: 'Good baseline for most repos. Fast indexing.',
            value: 'qwen3-embed-0.6b',
        },
        {
            label: 'qwen3-embed-4b',
            description: '2560-dim · ~3 GB · ~8 GB VRAM',
            detail: 'Higher recall on conceptual queries.',
            value: 'qwen3-embed-4b',
        },
        {
            label: 'qwen3-embed-8b',
            description: '4096-dim · ~6 GB · ~16 GB VRAM',
            detail: 'Highest recall. Indexing is slower; use only on big projects with a strong GPU.',
            value: 'qwen3-embed-8b',
        },
    ];
    const current = vscode.workspace
        .getConfiguration('vectra')
        .get<string>('indexModel', '')
        .trim();
    const picked = await vscode.window.showQuickPick(
        items.map((i) => ({
            ...i,
            description: i.value === current ? `${i.description} · current` : i.description,
        })),
        {
            title: 'Vectra: re-index with embedding model',
            placeHolder: 'pick the model the new index should use',
            matchOnDescription: true,
            matchOnDetail: true,
        },
    );
    if (!picked) return;

    // Persist BEFORE running so the auto-indexer (and every later
    // command) picks up the new value immediately.
    await vscode.workspace
        .getConfiguration('vectra')
        .update('indexModel', picked.value, vscode.ConfigurationTarget.Workspace);

    output.appendLine(
        `[vectra: switched index model to ${picked.value || '(symbol-only)'}; running fresh index]`,
    );

    const release = await indexLock.acquire();
    try {
        await runVectra(
            buildIndexArgs(),
            cwd,
            output,
            `Vectra is re-indexing with ${picked.label}…`,
        );
    } finally {
        release();
    }
}

export function activate(context: vscode.ExtensionContext): void {
    const output = vscode.window.createOutputChannel(OUTPUT_CHANNEL_NAME);
    // globalStorageUri is per-extension persistent storage that
    // survives reloads, machine reboots, and (intentionally)
    // workspace switches. Each workspace gets its own subdirectory
    // inside via ChatStorage's path hash, so two projects never
    // see each other's chat history.
    const storage = new ChatStorage(context.globalStorageUri);

    // PermissionBridge: HTTP listener on a random localhost port,
    // gated by a per-activation bearer token. The bundled MCP
    // permission-server.js POSTs each tool-use approval here; the
    // chat panel registers its listener once on first openChat. Bind
    // synchronously so listen() finishes before any chat panel
    // tries to writeMcpConfig() against bridge.url.
    //
    // Pass the output channel so the bridge logs each request /
    // resolve / denyAll into "Vectra" — the approval chain is four
    // hops long (webview → host → bridge → MCP → claude) and those
    // logs are what we lean on when something hangs.
    const bridge = new PermissionBridge(output);
    void bridge.start().catch((err) => {
        output.appendLine(
            `[vectra: permission bridge failed to start: ${err instanceof Error ? err.message : String(err)}]`,
        );
    });

    context.subscriptions.push(
        output,
        bridge,
        vscode.commands.registerCommand('vectra.ask', () => commandAsk(output)),
        vscode.commands.registerCommand('vectra.askAboutSelection', () =>
            commandAskAboutSelection(output),
        ),
        vscode.commands.registerCommand('vectra.index', () => commandIndex(output)),
        vscode.commands.registerCommand('vectra.reindexWithModel', () =>
            commandReindexWithModel(output),
        ),
        vscode.commands.registerCommand('vectra.newChat', () => VectraChatPanel.newChat()),
        vscode.commands.registerCommand('vectra.showHistory', () =>
            VectraChatPanel.showHistory(),
        ),
        vscode.commands.registerCommand('vectra.openChat', () => {
            VectraChatPanel.createOrShow(context.extensionUri, storage, bridge, output);
        }),
    );

    // Auto-indexer: only meaningful when there is a workspace folder.
    // The activation event in package.json (`workspaceContains:.vectra`)
    // is what eagerly starts the extension in Vectra-initialised
    // projects; here we just stand the watcher up. Projects without a
    // workspace folder still get the manual `vectra.ask` and
    // `vectra.index` commands.
    const cwd = workspaceRoot();
    if (cwd) {
        const autoIndexer = new AutoIndexer(cwd, output, () => readSettings().binary);
        context.subscriptions.push(autoIndexer);
    }
}

export function deactivate(): void {
    // Output channel is disposed via context.subscriptions.
}
