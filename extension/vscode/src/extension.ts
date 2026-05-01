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

import { VectraChatPanel } from './chatProvider';

const OUTPUT_CHANNEL_NAME = 'Vectra';

interface AskFlags {
    claudeModel?: string;
    effort?: string;
    topK?: number;
}

function readSettings(): { binary: string; flags: AskFlags } {
    const config = vscode.workspace.getConfiguration('vectra');
    const binary = config.get<string>('binary', 'vectra').trim() || 'vectra';
    const claudeModel = config.get<string>('claudeModel', '').trim();
    const effort = config.get<string>('effort', '').trim();
    const topK = config.get<number>('topK', 0);
    return {
        binary,
        flags: {
            claudeModel: claudeModel || undefined,
            effort: effort || undefined,
            topK: topK > 0 ? topK : undefined,
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
    if (flags.claudeModel) {
        args.push('--claude-model', flags.claudeModel);
    }
    if (flags.effort) {
        args.push('--effort', flags.effort);
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

async function commandIndex(output: vscode.OutputChannel): Promise<void> {
    const cwd = workspaceRoot();
    if (!cwd) {
        vscode.window.showErrorMessage('Vectra needs an open workspace folder.');
        return;
    }
    await runVectra(['index', '.'], cwd, output, 'Vectra is indexing the workspace…');
}

export function activate(context: vscode.ExtensionContext): void {
    const output = vscode.window.createOutputChannel(OUTPUT_CHANNEL_NAME);

    context.subscriptions.push(
        output,
        vscode.commands.registerCommand('vectra.ask', () => commandAsk(output)),
        vscode.commands.registerCommand('vectra.askAboutSelection', () =>
            commandAskAboutSelection(output),
        ),
        vscode.commands.registerCommand('vectra.index', () => commandIndex(output)),
        vscode.commands.registerCommand('vectra.newChat', () => VectraChatPanel.newChat()),
        vscode.commands.registerCommand('vectra.openChat', () => {
            VectraChatPanel.createOrShow(context.extensionUri);
        }),
    );
}

export function deactivate(): void {
    // Output channel is disposed via context.subscriptions.
}
