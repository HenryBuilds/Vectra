// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import { Composer } from './Composer';
import { Message } from './Message';
import { Toolbar } from './Toolbar';
import * as host from './vscode';
import type { ChatMessage, Inbound } from './types';

function newId(): string {
    return `t${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}

function EmptyState({ onAction }: { onAction(commandId: string): void }) {
    return (
        <div className="empty-state">
            <div className="empty-title">Ask Vectra anything about this codebase</div>
            Each turn runs hybrid retrieval (BM25 + embeddings) over the local index, then
            hands the top chunks to <code>claude -p</code>. Switch the model and thinking
            budget at the top of the panel.
            <div className="actions">
                <button
                    type="button"
                    className="action"
                    onClick={() => onAction('vectra.index')}
                >
                    Index this workspace
                </button>
            </div>
        </div>
    );
}

export function App() {
    const persisted = host.getState();

    const [history, setHistory] = React.useState<ChatMessage[]>(persisted?.history ?? []);
    const [model, setModel] = React.useState<string>(persisted?.model ?? '');
    const [effort, setEffort] = React.useState<string>(persisted?.effort ?? '');
    const [activeId, setActiveId] = React.useState<string | null>(null);

    const messagesEndRef = React.useRef<HTMLDivElement | null>(null);

    // Persist relevant slices of state whenever they change. Cheap;
    // setState is just a write to webview-internal storage.
    React.useEffect(() => {
        host.setState({ history, model, effort });
    }, [history, model, effort]);

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
                case 'chunk':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id ? { ...msg, body: msg.body + m.text } : msg,
                        ),
                    );
                    break;
                case 'meta':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id ? { ...msg, meta: msg.meta + m.text } : msg,
                        ),
                    );
                    break;
                case 'error':
                    setHistory((h) =>
                        h.map((msg) =>
                            msg.id === m.id
                                ? {
                                      ...msg,
                                      role: 'error',
                                      body: msg.body
                                          ? `${msg.body}\n\n${m.message}`
                                          : m.message,
                                      actions: m.actions ?? msg.actions,
                                  }
                                : msg,
                        ),
                    );
                    break;
                case 'done':
                    setActiveId((cur) => (cur === m.id ? null : cur));
                    break;
                case 'newChat':
                    setHistory([]);
                    setActiveId(null);
                    break;
                case 'config':
                    // Future: prefill model/effort from config the
                    // first time, never override the user's pick.
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
        const userMsg: ChatMessage = { id: `${id}-u`, role: 'user', body: text, meta: '' };
        const asstMsg: ChatMessage = { id, role: 'assistant', body: '', meta: '' };
        setHistory((h) => [...h, userMsg, asstMsg]);
        setActiveId(id);
        host.postMessage({
            type: 'send',
            id,
            text,
            model: model || undefined,
            effort: effort || undefined,
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

    return (
        <div className="root">
            <Toolbar
                model={model}
                effort={effort}
                onModelChange={setModel}
                onEffortChange={setEffort}
            />
            <main className="messages">
                {history.length === 0 ? (
                    <EmptyState onAction={handleAction} />
                ) : (
                    history.map((msg) => (
                        <Message
                            key={msg.id}
                            message={msg}
                            streaming={msg.id === activeId && msg.role === 'assistant'}
                            onAction={handleAction}
                        />
                    ))
                )}
                <div ref={messagesEndRef} />
            </main>
            <Composer busy={activeId !== null} onSend={handleSend} onCancel={handleCancel} />
        </div>
    );
}
