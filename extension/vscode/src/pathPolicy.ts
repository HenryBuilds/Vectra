// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Path-traversal guard for the in-chat approval bridge.
//
// Claude routes every tool call through `mcp__vectra__request_permission`
// when the user picks "ask" mode. The bridge in turn surfaces each
// request as a modal in the chat panel. This module sits *between*
// the bridge and the modal — any tool call that names a path is
// resolved against the active workspace folder, and anything that
// escapes the workspace is auto-denied without ever reaching the
// user. The user can always say "yes" to a path inside their
// workspace; they can never accidentally rubber-stamp `/etc/passwd`
// or `C:\Windows\System32\config\SAM`.
//
// Tools we gate (path-bearing):
//   Edit          — file_path
//   Write         — file_path
//   MultiEdit     — file_path
//   NotebookEdit  — notebook_path
//
// Tools we do *not* gate here:
//   Read          — read-only; gated by Claude Code's own boundary.
//   Bash          — needs full shell parsing to extract paths; the
//                   modal's risk-hint UI is the right surface for
//                   that today.
//
// "Inside the workspace" means: the absolute, normalised path is
// either equal to the workspace root or a descendant of it (with a
// proper path separator boundary, so `/work` does not match
// `/workshop/foo`). We do *not* follow symlinks — a symlink inside
// the workspace pointing outside it would still be allowed. That is
// a known weakness; closing it requires a syscall (`fs.realpath`)
// that fails when the file does not yet exist (which is the common
// case for Write), so we opt for prefix-based safety. A future
// hardening pass can use `realpath` for already-existing parents.

import * as path from 'path';

export type PathPolicyResult =
    | { kind: 'ok' }
    | { kind: 'no-workspace' }
    | { kind: 'no-path' }
    | { kind: 'deny'; reason: string; resolvedPath: string };

const PATH_BEARING_TOOLS: ReadonlyMap<string, readonly string[]> = new Map([
    ['Edit', ['file_path']],
    ['Write', ['file_path']],
    ['MultiEdit', ['file_path']],
    ['NotebookEdit', ['notebook_path']],
]);

/**
 * Decide whether to forward a permission request to the user.
 *
 * - `ok`           — tool/path is allowed to reach the modal.
 * - `no-workspace` — no workspace folder is open; the caller decides
 *                    what to do (default chat policy is to deny so
 *                    we never edit relative to the user's home dir).
 * - `no-path`      — the tool does not carry a path field we know
 *                    how to validate; forward as-is.
 * - `deny`         — the path resolves outside the workspace; the
 *                    caller should auto-deny without prompting.
 */
export function checkToolPath(
    toolName: string,
    toolInput: unknown,
    workspaceRoot: string | undefined,
): PathPolicyResult {
    const fields = PATH_BEARING_TOOLS.get(toolName);
    if (!fields) {
        return { kind: 'no-path' };
    }

    const raw = pickPathField(toolInput, fields);
    if (raw === undefined) {
        // Path-bearing tool called without its path field. Let the
        // modal show the user what claude actually sent — the
        // resulting tool_result error from claude is more useful
        // than a silent deny here.
        return { kind: 'no-path' };
    }

    if (!workspaceRoot) {
        return { kind: 'no-workspace' };
    }

    const verdict = isInsideWorkspace(raw, workspaceRoot);
    if (verdict.inside) {
        return { kind: 'ok' };
    }
    return {
        kind: 'deny',
        reason:
            `vectra: '${toolName}' was blocked because '${raw}' resolves to ` +
            `'${verdict.resolved}', which is outside the workspace at ` +
            `'${workspaceRoot}'. Approvals only apply to files inside the ` +
            `current workspace.`,
        resolvedPath: verdict.resolved,
    };
}

function pickPathField(input: unknown, fields: readonly string[]): string | undefined {
    if (!input || typeof input !== 'object') {
        return undefined;
    }
    const obj = input as Record<string, unknown>;
    for (const f of fields) {
        const v = obj[f];
        if (typeof v === 'string' && v.length > 0) {
            return v;
        }
        // Accept the camelCase variant as a fallback — claude has
        // historically drifted between snake_case and camelCase on
        // tool input fields.
        const camel = toCamel(f);
        if (camel !== f) {
            const cv = obj[camel];
            if (typeof cv === 'string' && cv.length > 0) {
                return cv;
            }
        }
    }
    return undefined;
}

function toCamel(s: string): string {
    return s.replace(/_([a-z])/g, (_, c: string) => c.toUpperCase());
}

interface InsideVerdict {
    inside: boolean;
    resolved: string;
}

/**
 * Resolve `candidate` (relative or absolute) against `workspaceRoot`,
 * normalise away `..`/`.` segments, and check that the result stays
 * inside the workspace. Symlinks are *not* followed — see module
 * docstring.
 */
export function isInsideWorkspace(candidate: string, workspaceRoot: string): InsideVerdict {
    // path.resolve handles both absolute and relative inputs:
    // - absolute `candidate` is returned as-is, normalised
    // - relative `candidate` is resolved against `workspaceRoot`
    const resolved = path.resolve(workspaceRoot, candidate);
    const root = path.resolve(workspaceRoot);

    // Compare with case sensitivity matching the host filesystem.
    // path.relative gives us the cleanest "is X under Y" check
    // because it normalises both sides and yields a string starting
    // with `..` if X escapes Y. An empty relative means X === Y,
    // which we count as inside (workspace root itself is fine).
    const rel = path.relative(root, resolved);
    if (rel === '') {
        return { inside: true, resolved };
    }
    // On Windows, paths on different drives produce a relative
    // starting with the drive letter (`D:\foo`). Treat that as
    // outside. path.isAbsolute on the relative is the cleanest
    // signal for the cross-drive case.
    if (path.isAbsolute(rel)) {
        return { inside: false, resolved };
    }
    if (rel.startsWith('..')) {
        return { inside: false, resolved };
    }
    return { inside: true, resolved };
}
