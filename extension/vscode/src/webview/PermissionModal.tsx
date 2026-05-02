// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Inline approval modal. Renders pinned to the bottom of the chat
// stack while at least one PermissionRequest is pending; when more
// than one queues up (claude wants to do several edits in a row)
// only the head of the queue is shown — the rest become visible as
// the user works through them.
//
// The component stays presentation-only: App.tsx owns the queue,
// posts the user's decision back to the host, and shifts the head
// off when the host settles the request.

import * as React from 'react';

import type { PermissionRequest } from './types';

interface PermissionModalProps {
    request: PermissionRequest;
    onApprove(): void;
    onDeny(): void;
}

// Reach into the typed-but-loose tool_input bag for the field shapes
// claude tends to send for the file-editing tools. Anything we don't
// recognise gets pretty-printed as JSON.
// Pull the first defined string out of a candidate field list. Used
// to bridge the gap between Claude Code's documented tool_input
// shape (`file_path`, `old_string`, `new_string`) and whatever name
// the live build happens to ship with — e.g. camelCase `filePath`,
// or a `path` alias.
function pickString(o: Record<string, unknown>, ...keys: string[]): string | null {
    for (const k of keys) {
        const v = o[k];
        if (typeof v === 'string') return v;
    }
    return null;
}

function describe(toolName: string, toolInput: unknown): React.ReactNode {
    if (typeof toolInput !== 'object' || toolInput === null) {
        return <pre className="permission-input">{String(toolInput)}</pre>;
    }
    const o = toolInput as Record<string, unknown>;
    // Show the raw input as a foldout under the structured view —
    // works as both a sanity check during development and as a
    // graceful fallback when claude ships a renamed field that the
    // structured branches do not catch.
    const rawDump = (
        <details className="permission-raw">
            <summary>raw input</summary>
            <pre className="permission-input">{JSON.stringify(o, null, 2)}</pre>
        </details>
    );

    if (toolName === 'Edit' || toolName === 'Write') {
        const file = pickString(o, 'file_path', 'filePath', 'path');
        const oldStr = pickString(o, 'old_string', 'oldString');
        const newStr = pickString(o, 'new_string', 'newString', 'content');
        // If we matched at least one of the recognised fields we
        // render the structured view; otherwise fall through to the
        // JSON dump so the user can still tell what claude wants.
        if (file !== null || oldStr !== null || newStr !== null) {
            return (
                <>
                    {file !== null && <div className="permission-file">{file}</div>}
                    {oldStr !== null && (
                        <div className="permission-diff">
                            <div className="permission-diff-label">−</div>
                            <pre className="permission-diff-old">{oldStr}</pre>
                        </div>
                    )}
                    {newStr !== null && (
                        <div className="permission-diff">
                            <div className="permission-diff-label">+</div>
                            <pre className="permission-diff-new">{newStr}</pre>
                        </div>
                    )}
                    {rawDump}
                </>
            );
        }
        return rawDump;
    }

    if (toolName === 'Bash') {
        const cmd = pickString(o, 'command', 'cmd');
        const desc = pickString(o, 'description', 'desc');
        if (cmd !== null || desc !== null) {
            return (
                <>
                    {desc !== null && <div className="permission-desc">{desc}</div>}
                    {cmd !== null && <pre className="permission-input">{cmd}</pre>}
                    {rawDump}
                </>
            );
        }
        return rawDump;
    }

    // Generic fallback for unknown tools — renders the full input as
    // pretty-printed JSON so the user has at least the raw arguments
    // to judge the request by.
    return <pre className="permission-input">{JSON.stringify(o, null, 2)}</pre>;
}

export function PermissionModal({ request, onApprove, onDeny }: PermissionModalProps) {
    return (
        <div className="permission-modal" role="dialog" aria-modal="true">
            <div className="permission-header">
                <span className="permission-tool">{request.toolName || 'tool'}</span>
                <span className="permission-prompt">requests permission</span>
            </div>
            <div className="permission-body">
                {describe(request.toolName, request.toolInput)}
            </div>
            <div className="permission-actions">
                <button type="button" className="permission-btn deny" onClick={onDeny}>
                    Deny
                </button>
                <button
                    type="button"
                    className="permission-btn approve"
                    onClick={onApprove}
                    autoFocus
                >
                    Approve
                </button>
            </div>
        </div>
    );
}
