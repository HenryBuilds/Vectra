// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

interface ToolbarProps {
    model: string;
    effort: string;
    onModelChange(model: string): void;
    onEffortChange(effort: string): void;
}

const MODEL_OPTIONS = [
    { value: '', label: 'model: default' },
    { value: 'haiku', label: 'haiku' },
    { value: 'sonnet', label: 'sonnet' },
    { value: 'opus', label: 'opus' },
];

const EFFORT_OPTIONS = [
    { value: '', label: 'effort: default' },
    { value: 'low', label: 'low' },
    { value: 'medium', label: 'medium' },
    { value: 'high', label: 'high' },
];

export function Toolbar({ model, effort, onModelChange, onEffortChange }: ToolbarProps) {
    return (
        <header className="toolbar">
            <select
                value={model}
                onChange={(e) => onModelChange(e.target.value)}
                title="Claude model passed as --claude-model"
            >
                {MODEL_OPTIONS.map((o) => (
                    <option key={o.value} value={o.value}>
                        {o.label}
                    </option>
                ))}
            </select>
            <select
                value={effort}
                onChange={(e) => onEffortChange(e.target.value)}
                title="Thinking budget passed as --effort"
            >
                {EFFORT_OPTIONS.map((o) => (
                    <option key={o.value} value={o.value}>
                        {o.label}
                    </option>
                ))}
            </select>
        </header>
    );
}
