// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import { ArrowUpIcon, XIcon } from './Icons';

interface ComposerProps {
    busy: boolean;
    onSend(text: string): void;
    onCancel(): void;
}

export function Composer({ busy, onSend, onCancel }: ComposerProps) {
    const [text, setText] = React.useState('');
    const taRef = React.useRef<HTMLTextAreaElement | null>(null);

    // Grow the textarea with content (up to a cap) so the composer
    // expands like a real chat input.
    React.useEffect(() => {
        const el = taRef.current;
        if (!el) {
            return;
        }
        el.style.height = '0';
        const next = Math.min(el.scrollHeight, 220);
        el.style.height = `${next}px`;
    }, [text]);

    const submit = () => {
        const t = text.trim();
        if (!t || busy) {
            return;
        }
        onSend(t);
        setText('');
    };

    const handleKeyDown = (e: React.KeyboardEvent<HTMLTextAreaElement>) => {
        // Bare Enter sends, Shift+Enter inserts a newline. This is the
        // chat convention every modern AI UI uses; multi-line prompts
        // are rare enough that the inverted default would frustrate
        // more often than help.
        if (e.key === 'Enter' && !e.shiftKey && !e.ctrlKey && !e.metaKey) {
            e.preventDefault();
            submit();
        }
    };

    return (
        <footer className="composer">
            <div className={`composer-card${busy ? ' busy' : ''}`}>
                <textarea
                    ref={taRef}
                    rows={1}
                    value={text}
                    placeholder="Ask Vectra…"
                    onChange={(e) => setText(e.target.value)}
                    onKeyDown={handleKeyDown}
                    disabled={busy}
                />
                <div className="composer-actions">
                    {busy ? (
                        <button
                            type="button"
                            className="icon-btn cancel"
                            onClick={onCancel}
                            title="Cancel"
                            aria-label="Cancel"
                        >
                            <XIcon />
                        </button>
                    ) : (
                        <button
                            type="button"
                            className="icon-btn send"
                            onClick={submit}
                            disabled={text.trim().length === 0}
                            title="Send (Enter)"
                            aria-label="Send"
                        >
                            <ArrowUpIcon />
                        </button>
                    )}
                </div>
            </div>
            <div className="composer-hint">
                <span>Enter to send · Shift+Enter for newline</span>
            </div>
        </footer>
    );
}
