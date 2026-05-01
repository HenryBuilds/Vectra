// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import type { ChatMessage } from './types';

interface MessageProps {
    message: ChatMessage;
    streaming: boolean;
    onAction(commandId: string): void;
}

const ROLE_LABELS: Record<ChatMessage['role'], string> = {
    user: 'You',
    assistant: 'Vectra',
    error: 'Error',
};

export function Message({ message, streaming, onAction }: MessageProps) {
    const className = `message ${message.role}${streaming ? ' streaming' : ''}`;
    return (
        <div className={className}>
            <div className="role">{ROLE_LABELS[message.role]}</div>
            {message.meta && <div className="meta">{message.meta}</div>}
            {message.body && <div className="body">{message.body}</div>}
            {message.actions && message.actions.length > 0 && (
                <div className="actions">
                    {message.actions.map((a) => (
                        <button
                            key={a.commandId}
                            type="button"
                            className="action"
                            onClick={() => onAction(a.commandId)}
                        >
                            {a.label}
                        </button>
                    ))}
                </div>
            )}
        </div>
    );
}
