// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Auto-indexer: watches the workspace and re-runs `vectra index .` after
// files change. The CLI uses BLAKE3 file hashes and skips unchanged
// files, so a re-run after a single edit is cheap — typically tens of
// milliseconds even on a multi-thousand-file repo.
//
// Three concerns this module owns:
//   1. Coalescing bursts of file events into one index run via a
//      configurable debounce window.
//   2. Preventing two index processes from racing on the same .vectra/
//      database. A second run that arrives mid-flight is queued, not
//      spawned in parallel.
//   3. Quiet feedback in the status bar so the user knows when an
//      auto-index is in flight without a popup interrupting them.
//
// Out of scope: deciding whether to index. The activation event in
// package.json already gates this — the AutoIndexer only runs in
// workspaces that contain a .vectra/ directory.

import * as cp from 'child_process';
import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

// File events under these prefixes are ignored. `.vectra/` matters
// because the indexer writes its own database there — without this
// filter every reindex would trigger another reindex, forever. `.git/`
// matters because git operations (checkout, rebase) generate noisy
// bursts of events that have nothing to do with source changes.
const IGNORE_PREFIXES = ['.vectra/', '.git/'];

export class AutoIndexer implements vscode.Disposable {
    private readonly watcher: vscode.FileSystemWatcher;
    private readonly statusBar: vscode.StatusBarItem;
    private readonly disposables: vscode.Disposable[] = [];
    private debounceTimer: NodeJS.Timeout | undefined;
    private hideTimer: NodeJS.Timeout | undefined;
    private running = false;
    private pending = false;

    constructor(
        private readonly cwd: string,
        private readonly output: vscode.OutputChannel,
        private readonly resolveBinary: () => string,
    ) {
        // Watch the entire workspace. The CLI's walker handles per-file
        // filtering by extension, gitignore, and the universal-skip
        // directory list, so we don't duplicate that logic here. The
        // debounce collapses bursts into a single index run.
        this.watcher = vscode.workspace.createFileSystemWatcher('**/*');
        this.disposables.push(
            this.watcher.onDidChange((uri) => this.onChange(uri)),
            this.watcher.onDidCreate((uri) => this.onChange(uri)),
            this.watcher.onDidDelete((uri) => this.onChange(uri)),
        );

        this.statusBar = vscode.window.createStatusBarItem(
            vscode.StatusBarAlignment.Right,
            100,
        );
        this.statusBar.tooltip = 'Vectra auto-indexer';
        // Click goes to the manual index command — useful when the
        // user wants to force a re-run without waiting for a save.
        this.statusBar.command = 'vectra.index';
    }

    private onChange(uri: vscode.Uri): void {
        const config = vscode.workspace.getConfiguration('vectra');
        if (!config.get<boolean>('autoIndex', true)) {
            return;
        }

        // asRelativePath returns OS-native separators on Windows; we
        // normalise to forward slashes before the prefix comparison
        // so the IGNORE_PREFIXES list does not need both spellings.
        const rel = vscode.workspace.asRelativePath(uri).replace(/\\/g, '/');
        if (IGNORE_PREFIXES.some((p) => rel.startsWith(p))) {
            return;
        }

        const debounceMs = config.get<number>('autoIndexDebounceMs', 2000);
        if (this.debounceTimer !== undefined) {
            clearTimeout(this.debounceTimer);
        }
        this.debounceTimer = setTimeout(() => {
            this.debounceTimer = undefined;
            void this.runReindex();
        }, debounceMs);
    }

    private async runReindex(): Promise<void> {
        // Defensive check: the activation event only fires for workspaces
        // that already contain .vectra/, but that directory could be
        // deleted at runtime (git clean, rm -rf .vectra, …). Skipping
        // here is preferable to spamming the user with "no project root"
        // errors on every save.
        if (!fs.existsSync(path.join(this.cwd, '.vectra'))) {
            return;
        }

        if (this.running) {
            // A reindex is already in flight. Mark a follow-up so we
            // pick up whatever changed while we were busy, but do not
            // spawn a second process — the .vectra/ SQLite database
            // tolerates concurrent readers but not concurrent writers.
            this.pending = true;
            return;
        }

        this.running = true;
        if (this.hideTimer !== undefined) {
            clearTimeout(this.hideTimer);
            this.hideTimer = undefined;
        }
        this.statusBar.text = '$(sync~spin) Vectra: indexing…';
        this.statusBar.show();

        try {
            await this.spawnIndex();
        } finally {
            this.running = false;
            this.statusBar.text = '$(database) Vectra: indexed';
            // Hide after a short dwell so the user catches the fact that
            // it ran, but the bar does not stay cluttered indefinitely.
            this.hideTimer = setTimeout(() => {
                this.hideTimer = undefined;
                this.statusBar.hide();
            }, 2000);

            if (this.pending) {
                this.pending = false;
                void this.runReindex();
            }
        }
    }

    private spawnIndex(): Promise<void> {
        const binary = this.resolveBinary();
        return new Promise<void>((resolve) => {
            this.output.appendLine(`> ${binary} index . --quiet`);
            let proc: cp.ChildProcess;
            try {
                proc = cp.spawn(binary, ['index', '.', '--quiet'], {
                    cwd: this.cwd,
                    env: process.env,
                });
            } catch (err) {
                const msg = err instanceof Error ? err.message : String(err);
                this.output.appendLine(`auto-index: spawn failed: ${msg}`);
                resolve();
                return;
            }

            proc.stdout?.on('data', (d: Buffer) => this.output.append(d.toString('utf8')));
            proc.stderr?.on('data', (d: Buffer) => this.output.append(d.toString('utf8')));
            proc.on('error', (err) => {
                this.output.appendLine(`auto-index error: ${err.message}`);
                resolve();
            });
            proc.on('close', (code) => {
                this.output.appendLine(`[auto-index exited with code ${code ?? 0}]`);
                resolve();
            });
        });
    }

    dispose(): void {
        if (this.debounceTimer !== undefined) {
            clearTimeout(this.debounceTimer);
        }
        if (this.hideTimer !== undefined) {
            clearTimeout(this.hideTimer);
        }
        this.disposables.forEach((d) => d.dispose());
        this.watcher.dispose();
        this.statusBar.dispose();
    }
}
