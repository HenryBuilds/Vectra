// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Persisted "always allow" rules for the in-chat approval flow.
//
// When the user clicks "Always allow" on a permission modal, we
// remember (toolName, pattern) so future requests that match the
// same shape skip the modal entirely. Storage is per-workspace via
// the extension's workspaceState memento — patterns trusted in one
// project must NOT leak into another. Nothing here ever touches
// settings.json or globalState.
//
// Pattern shape per tool:
//
//   Edit / Write / MultiEdit / NotebookEdit
//       Pattern is a workspace-relative path. For now the match is
//       exact (after normalisation). Glob support is intentionally
//       deferred — a glob "always allow src/**" is a much sharper
//       footgun than "always allow src/foo/bar.ts" and we want
//       users to feel the friction the first time.
//
//   Bash
//       Pattern is a command-prefix string. We compare token-by-
//       token (whitespace-split) so "npm test" matches both
//       `npm test` and `npm test --verbose`, but not `npm install`.
//
//   Anything else
//       Pattern is the literal string '*' meaning "any input for
//       this tool". Useful for read-only tools where there is
//       nothing meaningful to scope on.

import * as path from 'path';
import * as vscode from 'vscode';

const STORAGE_KEY = 'vectra.allowList.v1';

export interface AllowEntry {
    toolName: string;
    pattern: string;
}

export class AllowList {
    private cache: AllowEntry[] = [];

    constructor(
        private readonly memento: vscode.Memento,
        private readonly workspaceRoot: string,
    ) {
        const raw = this.memento.get<unknown>(STORAGE_KEY, []);
        if (Array.isArray(raw)) {
            for (const v of raw) {
                if (
                    v &&
                    typeof v === 'object' &&
                    typeof (v as AllowEntry).toolName === 'string' &&
                    typeof (v as AllowEntry).pattern === 'string'
                ) {
                    this.cache.push({
                        toolName: (v as AllowEntry).toolName,
                        pattern: (v as AllowEntry).pattern,
                    });
                }
            }
        }
    }

    list(): readonly AllowEntry[] {
        return this.cache.slice();
    }

    /**
     * Return true if the given tool call is covered by an existing
     * always-allow entry. A miss requires user confirmation.
     */
    matches(toolName: string, toolInput: unknown): boolean {
        for (const entry of this.cache) {
            if (entry.toolName !== toolName) continue;
            if (this.entryMatches(entry, toolInput)) {
                return true;
            }
        }
        return false;
    }

    /**
     * Build the pattern we suggest when the user clicks "Always
     * allow" on a request. The caller can present this in a confirm
     * dialog, save it directly, or discard it. Returns null when we
     * have no sensible scope (e.g. malformed tool input).
     */
    suggestPattern(toolName: string, toolInput: unknown): string | null {
        if (toolName === 'Edit' || toolName === 'Write' || toolName === 'MultiEdit' || toolName === 'NotebookEdit') {
            const p = pickPath(toolInput, toolName);
            if (!p) return null;
            return this.workspaceRelative(p);
        }
        if (toolName === 'Bash') {
            const cmd = pickField(toolInput, ['command', 'cmd']);
            if (!cmd) return null;
            return commandPrefix(cmd);
        }
        return '*';
    }

    async add(entry: AllowEntry): Promise<void> {
        if (!entry.toolName || !entry.pattern) return;
        // De-dup on (toolName, pattern). Adding the same rule twice
        // is a no-op.
        const exists = this.cache.some(
            (e) => e.toolName === entry.toolName && e.pattern === entry.pattern,
        );
        if (exists) return;
        this.cache.push({ toolName: entry.toolName, pattern: entry.pattern });
        await this.memento.update(STORAGE_KEY, this.cache);
    }

    async remove(entry: AllowEntry): Promise<void> {
        const before = this.cache.length;
        this.cache = this.cache.filter(
            (e) => !(e.toolName === entry.toolName && e.pattern === entry.pattern),
        );
        if (this.cache.length !== before) {
            await this.memento.update(STORAGE_KEY, this.cache);
        }
    }

    async clear(): Promise<void> {
        if (this.cache.length === 0) return;
        this.cache = [];
        await this.memento.update(STORAGE_KEY, this.cache);
    }

    private entryMatches(entry: AllowEntry, toolInput: unknown): boolean {
        if (entry.pattern === '*') {
            return true;
        }
        if (
            entry.toolName === 'Edit' ||
            entry.toolName === 'Write' ||
            entry.toolName === 'MultiEdit' ||
            entry.toolName === 'NotebookEdit'
        ) {
            const p = pickPath(toolInput, entry.toolName);
            if (!p) return false;
            const candidate = this.workspaceRelative(p);
            return candidate === entry.pattern;
        }
        if (entry.toolName === 'Bash') {
            const cmd = pickField(toolInput, ['command', 'cmd']);
            if (!cmd) return false;
            return commandPrefixMatches(cmd, entry.pattern);
        }
        return false;
    }

    /**
     * Normalise a workspace-relative path so two equivalent forms
     * (`./src/foo`, `src\\foo`, absolute path under root) collapse
     * to the same entry. Anything outside the workspace returns
     * the absolute resolved form so the path-policy guard catches
     * it before we even ask.
     */
    private workspaceRelative(p: string): string {
        const abs = path.resolve(this.workspaceRoot, p);
        const rel = path.relative(this.workspaceRoot, abs);
        if (rel === '' || rel.startsWith('..') || path.isAbsolute(rel)) {
            return abs;
        }
        return rel.split(path.sep).join('/');
    }
}

function pickField(input: unknown, keys: readonly string[]): string | null {
    if (!input || typeof input !== 'object') return null;
    const o = input as Record<string, unknown>;
    for (const k of keys) {
        const v = o[k];
        if (typeof v === 'string' && v.length > 0) return v;
    }
    return null;
}

function pickPath(input: unknown, toolName: string): string | null {
    if (toolName === 'NotebookEdit') {
        return pickField(input, ['notebook_path', 'notebookPath']);
    }
    return pickField(input, ['file_path', 'filePath', 'path']);
}

/**
 * Build the "first two tokens" prefix of a shell command. Trims
 * leading whitespace, splits on any run of whitespace, and joins
 * the first one or two tokens with a single space. The result is
 * the suggested pattern we offer when the user clicks "Always
 * allow" on a Bash request.
 *
 * Examples:
 *   "  npm   test --verbose"  → "npm test"
 *   "ls -la"                  → "ls -la"
 *   "git status"              → "git status"
 *   "echo hi"                 → "echo hi"
 */
export function commandPrefix(cmd: string): string {
    const tokens = cmd.trim().split(/\s+/).filter((t) => t.length > 0);
    if (tokens.length === 0) return '';
    if (tokens.length === 1) return tokens[0];
    return `${tokens[0]} ${tokens[1]}`;
}

/**
 * Token-prefix match. `pattern` covers `cmd` if the whitespace-
 * separated token list of pattern is a prefix of cmd's. Matches
 * are exact at each token (no glob inside tokens) — keeps the
 * "always allow" semantics predictable.
 */
export function commandPrefixMatches(cmd: string, pattern: string): boolean {
    const cmdTokens = cmd.trim().split(/\s+/).filter((t) => t.length > 0);
    const patTokens = pattern.trim().split(/\s+/).filter((t) => t.length > 0);
    if (patTokens.length === 0) return false;
    if (patTokens.length > cmdTokens.length) return false;
    for (let i = 0; i < patTokens.length; ++i) {
        if (cmdTokens[i] !== patTokens[i]) return false;
    }
    return true;
}
