// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

interface ToolbarProps {
    model: string;
    effort: string;
    permissionMode: string;
    onModelChange(model: string): void;
    onEffortChange(effort: string): void;
    onPermissionModeChange(mode: string): void;
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

// Mirrors the Claude Code mode picker. Empty value means "fall back
// to the vectra.permissionMode workspace setting"; the rest map 1:1
// to the values claude -p accepts.
const PERMISSION_MODE_OPTIONS = [
    { value: '', label: 'mode: settings' },
    { value: 'default', label: 'ask before edits' },
    { value: 'acceptEdits', label: 'auto-edit' },
    { value: 'plan', label: 'plan only' },
    { value: 'bypassPermissions', label: 'bypass permissions' },
];

export function Toolbar({
    model,
    effort,
    permissionMode,
    onModelChange,
    onEffortChange,
    onPermissionModeChange,
}: ToolbarProps) {
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
            <select
                value={permissionMode}
                onChange={(e) => onPermissionModeChange(e.target.value)}
                title="Permission mode passed as --permission-mode (mirrors Claude Code's mode picker)"
            >
                {PERMISSION_MODE_OPTIONS.map((o) => (
                    <option key={o.value} value={o.value}>
                        {o.label}
                    </option>
                ))}
            </select>
        </header>
    );
}
