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
function describe(toolName: string, toolInput: unknown): React.ReactNode {
    if (typeof toolInput !== 'object' || toolInput === null) {
        return <pre className="permission-input">{String(toolInput)}</pre>;
    }
    const o = toolInput as Record<string, unknown>;

    if (toolName === 'Edit' || toolName === 'Write') {
        const file = typeof o.file_path === 'string' ? o.file_path : '<unknown file>';
        const oldStr = typeof o.old_string === 'string' ? o.old_string : null;
        const newStr =
            typeof o.new_string === 'string'
                ? o.new_string
                : typeof o.content === 'string'
                  ? o.content
                  : null;
        return (
            <>
                <div className="permission-file">{file}</div>
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
            </>
        );
    }

    if (toolName === 'Bash') {
        const cmd = typeof o.command === 'string' ? o.command : '';
        const desc = typeof o.description === 'string' ? o.description : '';
        return (
            <>
                {desc.length > 0 && <div className="permission-desc">{desc}</div>}
                <pre className="permission-input">{cmd}</pre>
            </>
        );
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
