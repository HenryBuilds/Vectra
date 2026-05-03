// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import { Composer } from './Composer';
import { Message } from './Message';
import { PermissionModal } from './PermissionModal';
import { Toolbar } from './Toolbar';
import * as host from './vscode';
import type { ChatMessage, Inbound, MessageBlock, PermissionRequest } from './types';

function newId(): string {
    return `t${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

// Pick a short, human-friendly title for the session. The first
// user message wins — it's almost always what the chat is "about".
// Title is shown in the panel tab and the history QuickPick.
function deriveTitle(messages: ChatMessage[]): string {
    for (const msg of messages) {
        if (msg.role !== 'user') continue;
        for (const block of msg.blocks) {
            if (block.kind === 'text' && block.text.trim().length > 0) {
                return block.text.replace(/\s+/g, ' ').trim().slice(0, 200);
            }
        }
    }
    return '(untitled)';
}

// Older builds persisted ChatMessage with `body: string` instead of
// `blocks: MessageBlock[]`. Convert on read so a window reload after
// the upgrade does not blank the chat.
function migrateMessage(msg: unknown): ChatMessage | null {
    if (!msg || typeof msg !== 'object') {
        return null;
    }
    const m = msg as {
        id?: unknown;
        role?: unknown;
        body?: unknown;
        blocks?: unknown;
        meta?: unknown;
        actions?: unknown;
        usage?: unknown;
    };
    if (typeof m.id !== 'string' || typeof m.role !== 'string') {
        return null;
    }
    const role = m.role as ChatMessage['role'];
    const meta = typeof m.meta === 'string' ? m.meta : '';
    let blocks: MessageBlock[];
    if (Array.isArray(m.blocks)) {
        blocks = m.blocks as MessageBlock[];
    } else if (typeof m.body === 'string' && m.body.length > 0) {
        blocks = [{ kind: 'text', text: m.body }];
    } else {
        blocks = [];
    }
    return {
        id: m.id,
        role,
        blocks,
        meta,
        actions: m.actions as ChatMessage['actions'],
        usage: m.usage as ChatMessage['usage'],
    };
}

function migrateHistory(raw: unknown): ChatMessage[] {
    if (!Array.isArray(raw)) {
        return [];
    }
    const out: ChatMessage[] = [];
    for (const m of raw) {
        const migrated = migrateMessage(m);
        if (migrated) {
            out.push(migrated);
        }
    }
    return out;
}

interface EmptyStateProps {
    onAction(commandId: string): void;
    indexExists: boolean;
    indexModel: string;
    reranker: string;
}

function EmptyState({ onAction, indexExists, indexModel, reranker }: EmptyStateProps) {
    return (
        <div className="empty-state">
            <div className="empty-title">Ask Vectra anything about this codebase</div>
            Each turn runs hybrid retrieval (BM25 + embeddings) over the local index, then
            hands the top chunks to <code>claude -p</code>. Switch the model and thinking
            budget at the top of the panel.
            <div className="empty-config">
                <div className="empty-config-row">
                    <span className="empty-config-label">embedder</span>
                    <span className="empty-config-value">
                        {indexModel || 'symbol-only (no embeddings)'}
                    </span>
                </div>
                <div className="empty-config-row">
                    <span className="empty-config-label">reranker</span>
                    <span className="empty-config-value">{reranker || 'off'}</span>
                </div>
                <div className="empty-config-actions">
                    <button
                        type="button"
                        className="action-link"
                        onClick={() => onAction('vectra.reindexWithModel')}
                    >
                        Change model & re-index…
                    </button>
                </div>
            </div>
            {!indexExists && (
                <div className="actions">
                    <button
                        type="button"
                        className="action"
                        onClick={() => onAction('vectra.index')}
                    >
                        Index this workspace
                    </button>
                </div>
            )}
        </div>
    );
}

export function App() {
    const persisted = host.getState();

    const [history, setHistory] = React.useState<ChatMessage[]>(
        migrateHistory(persisted?.history),
    );
    const [model, setModel] = React.useState<string>(persisted?.model ?? '');
    const [effort, setEffort] = React.useState<string>(persisted?.effort ?? '');
    const [permissionMode, setPermissionMode] = React.useState<string>(
        persisted?.permissionMode ?? '',
    );
    const [activeId, setActiveId] = React.useState<string | null>(null);
    // Pending permission requests — FIFO queue. The head is rendered
    // as the active modal; once the user decides we shift it off and
    // the next one becomes visible. Not persisted: in-flight
    // approvals do not survive a webview reload, the bridge denies
    // them on the host side.
    const [pendingPermissions, setPendingPermissions] = React.useState<PermissionRequest[]>(
        [],
    );
    // Persisted on the host side via the `config` event from
    // chatProvider.ts. Default true to avoid flashing the
    // "Index this workspace" CTA before the host has a chance to
    // tell us the index is already there.
    const [indexExists, setIndexExists] = React.useState<boolean>(true);
    const [indexModel, setIndexModel] = React.useState<string>('');
    const [reranker, setReranker] = React.useState<string>('');

    const messagesEndRef = React.useRef<HTMLDivElement | null>(null);

    // Persist relevant slices of state whenever they change. Cheap;
    // setState is just a write to webview-internal storage.
    React.useEffect(() => {
        host.setState({ history, model, effort, permissionMode });
    }, [history, model, effort, permissionMode]);

    // Scroll to bottom on every message update.
    React.useEffect(() => {
        messagesEndRef.current?.scrollIntoView({ block: 'end' });
    }, [history]);

    // Wire incoming postMessage events.
    React.useEffect(() => {
        const handler = (event: MessageEvent) => {
            const m = event.data as Inbound;
            switch (m.type) {
                case 'started':
                    // Visual streaming state is implied by activeId.
                    break;
                case 'meta':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id ? { ...msg, meta: msg.meta + m.text } : msg,
                        ),
                    );
                    break;
                case 'block_start':
                    setHistory((h) =>
                        h.map((msg) => {
                            if (msg.id !== m.id) return msg;
                            const blocks = [...msg.blocks];
                            // Pad to the requested index. claude
                            // numbers blocks 0..N-1 in emit order so
                            // gaps should not happen, but tolerate
                            // them with empty text placeholders.
                            while (blocks.length <= m.blockIndex) {
                                blocks.push({ kind: 'text', text: '' });
                            }
                            if (m.block.kind === 'text') {
                                blocks[m.blockIndex] = { kind: 'text', text: '' };
                            } else if (m.block.kind === 'tool_use') {
                                blocks[m.blockIndex] = {
                                    kind: 'tool_use',
                                    toolUseId: m.block.toolUseId,
                                    name: m.block.name,
                                    inputRaw: '',
                                };
                            } else {
                                blocks[m.blockIndex] = { kind: 'thinking', text: '' };
                            }
                            return { ...msg, blocks };
                        }),
                    );
                    break;
                case 'block_delta':
                    setHistory((h) =>
                        h.map((msg) => {
                            if (msg.id !== m.id) return msg;
                            const blocks = [...msg.blocks];
                            const block = blocks[m.blockIndex];
                            if (!block) return msg;
                            if (m.delta.kind === 'text' && block.kind === 'text') {
                                blocks[m.blockIndex] = {
                                    ...block,
                                    text: block.text + m.delta.text,
                                };
                            } else if (
                                m.delta.kind === 'tool_input' &&
                                block.kind === 'tool_use'
                            ) {
                                blocks[m.blockIndex] = {
                                    ...block,
                                    inputRaw: block.inputRaw + m.delta.partialJson,
                                };
                            } else if (
                                m.delta.kind === 'thinking' &&
                                block.kind === 'thinking'
                            ) {
                                blocks[m.blockIndex] = {
                                    ...block,
                                    text: block.text + m.delta.text,
                                };
                            }
                            return { ...msg, blocks };
                        }),
                    );
                    break;
                case 'block_stop':
                    // No-op for now. Could flip a per-block "done"
                    // flag if we want to drop streaming-caret styling
                    // when the block finishes.
                    break;
                case 'tool_result':
                    setHistory((h) =>
                        h.map((msg) => {
                            if (msg.id !== m.id) return msg;
                            let mutated = false;
                            const blocks = msg.blocks.map((block) => {
                                if (
                                    block.kind === 'tool_use' &&
                                    block.toolUseId === m.toolUseId
                                ) {
                                    mutated = true;
                                    return {
                                        ...block,
                                        result: { content: m.content, isError: m.isError },
                                    };
                                }
                                return block;
                            });
                            return mutated ? { ...msg, blocks } : msg;
                        }),
                    );
                    break;
                case 'usage':
                    setHistory((h) =>
                        h.map((msg) => (msg.id === m.id ? { ...msg, usage: m.usage } : msg)),
                    );
                    break;
                case 'sources':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id ? { ...msg, sources: m.sources } : msg,
                        ),
                    );
                    break;
                case 'chunk':
                    // Legacy fallback for non-JSON stdout (older
                    // claude binary, raw error text). Append to the
                    // trailing text block or create one.
                    setHistory((h) =>
                        h.map((msg) => {
                            if (msg.id !== m.id) return msg;
                            const blocks = [...msg.blocks];
                            const last = blocks[blocks.length - 1];
                            if (last && last.kind === 'text') {
                                blocks[blocks.length - 1] = {
                                    ...last,
                                    text: last.text + m.text,
                                };
                            } else {
                                blocks.push({ kind: 'text', text: m.text });
                            }
                            return { ...msg, blocks };
                        }),
                    );
                    break;
                case 'error':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id
                                ? {
                                      ...msg,
                                      role: 'error',
                                      blocks: [
                                          ...msg.blocks,
                                          { kind: 'text', text: m.message },
                                      ],
                                      actions: m.actions ?? msg.actions,
                                  }
                                : msg,
                        ),
                    );
                    break;
                case 'done':
                    setActiveId((cur) => (cur === m.id ? null : cur));
                    // The turn is settled; persist the whole
                    // session to disk so a reload picks up exactly
                    // where we left off. The host stamps
                    // updatedAt and writes the JSON.
                    setHistory((h) => {
                        if (h.length > 0) {
                            host.postMessage({
                                type: 'saveSession',
                                title: deriveTitle(h),
                                messages: h,
                            });
                        }
                        return h;
                    });
                    break;
                case 'newChat':
                    setHistory([]);
                    setActiveId(null);
                    break;
                case 'config':
                    // Future: prefill model/effort from config the
                    // first time, never override the user's pick.
                    setIndexExists(m.indexExists);
                    setIndexModel(m.indexModel ?? '');
                    setReranker(m.reranker ?? '');
                    break;
                case 'sessionLoaded':
                    // Host pushed a session into us — either the
                    // most recent one on panel mount, or the one
                    // the user picked from the history QuickPick.
                    // Replace the chat entirely; null means "start
                    // fresh".
                    if (m.session) {
                        setHistory(migrateHistory(m.session.messages));
                    } else {
                        setHistory([]);
                    }
                    setActiveId(null);
                    break;
                case 'permissionRequest':
                    setPendingPermissions((q) => [
                        ...q,
                        {
                            requestId: m.requestId,
                            toolName: m.toolName,
                            toolInput: m.toolInput,
                            toolUseId: m.toolUseId,
                        },
                    ]);
                    break;
            }
        };
        window.addEventListener('message', handler);
        return () => window.removeEventListener('message', handler);
    }, []);

    // Initial handshake.
    React.useEffect(() => {
        host.postMessage({ type: 'ready' });
    }, []);

    const handleSend = (text: string) => {
        if (activeId !== null) {
            return;
        }
        const id = newId();
        const userMsg: ChatMessage = {
            id: `${id}-u`,
            role: 'user',
            blocks: [{ kind: 'text', text }],
            meta: '',
        };
        const asstMsg: ChatMessage = { id, role: 'assistant', blocks: [], meta: '' };
        setHistory((h) => [...h, userMsg, asstMsg]);
        setActiveId(id);
        host.postMessage({
            type: 'send',
            id,
            text,
            model: model || undefined,
            effort: effort || undefined,
            permissionMode: permissionMode || undefined,
        });
    };

    const handleCancel = () => {
        if (!activeId) {
            return;
        }
        host.postMessage({ type: 'cancel', id: activeId });
    };

    const handleAction = (commandId: string) => {
        host.postMessage({ type: 'action', commandId });
    };

    const handleOpenFile = (path: string, line: number) => {
        host.postMessage({ type: 'openFile', path, line });
    };

    // Settle the head of the permission queue. Posts the user's
    // decision to the host (which lets the bridge HTTP response
    // through) and shifts the head off so the next modal — if any
    // — slides in. The `alwaysAllow` flag is set when the user
    // clicked "Always allow" so the host persists a matching rule.
    const handlePermissionDecision = (
        request: PermissionRequest,
        decision: 'allow' | 'deny',
        opts?: { alwaysAllow?: boolean },
    ) => {
        host.postMessage({
            type: 'permissionResponse',
            requestId: request.requestId,
            decision,
            alwaysAllow: opts?.alwaysAllow === true ? true : undefined,
        });
        setPendingPermissions((q) => q.filter((r) => r.requestId !== request.requestId));
    };

    return (
        <div className="root">
            <Toolbar
                model={model}
                effort={effort}
                permissionMode={permissionMode}
                onModelChange={setModel}
                onEffortChange={setEffort}
                onPermissionModeChange={setPermissionMode}
            />
            <main className="messages">
                {history.length === 0 ? (
                    <EmptyState
                        onAction={handleAction}
                        indexExists={indexExists}
                        indexModel={indexModel}
                        reranker={reranker}
                    />
                ) : (
                    history.map((msg) => (
                        <Message
                            key={msg.id}
                            message={msg}
                            streaming={msg.id === activeId && msg.role === 'assistant'}
                            onAction={handleAction}
                            onOpenFile={handleOpenFile}
                        />
                    ))
                )}
                <div ref={messagesEndRef} />
            </main>
            {pendingPermissions.length > 0 && (
                <PermissionModal
                    request={pendingPermissions[0]}
                    onApprove={(opts) =>
                        handlePermissionDecision(pendingPermissions[0], 'allow', opts)
                    }
                    onDeny={() => handlePermissionDecision(pendingPermissions[0], 'deny')}
                />
            )}
            <Composer busy={activeId !== null} onSend={handleSend} onCancel={handleCancel} />
        </div>
    );
}
