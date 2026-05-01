// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import type {
    ChatMessage,
    MessageBlock,
    SourceChunk,
    ThinkingBlock,
    ToolUseBlock,
    UsageData,
} from './types';

interface MessageProps {
    message: ChatMessage;
    streaming: boolean;
    onAction(commandId: string): void;
    onOpenFile(path: string, line: number): void;
}

const ROLE_LABELS: Record<ChatMessage['role'], string> = {
    user: 'You',
    assistant: 'Vectra',
    error: 'Error',
};

export function Message({ message, streaming, onAction, onOpenFile }: MessageProps) {
    const className = `message ${message.role}${streaming ? ' streaming' : ''}`;
    return (
        <div className={className}>
            <div className="role">{ROLE_LABELS[message.role]}</div>
            {message.meta && <div className="meta">{message.meta}</div>}
            {message.blocks.map((block, i) => (
                <BlockRenderer key={i} block={block} />
            ))}
            {message.sources && message.sources.length > 0 && (
                <SourcesFooter sources={message.sources} onOpen={onOpenFile} />
            )}
            {message.usage && <UsageFooter usage={message.usage} />}
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

function BlockRenderer({ block }: { block: MessageBlock }) {
    if (block.kind === 'text') {
        return block.text ? <div className="body">{block.text}</div> : null;
    }
    if (block.kind === 'tool_use') {
        return <ToolUseRender block={block} />;
    }
    return <ThinkingRender block={block} />;
}

// Render a tool_use block as a collapsible badge: tool name +
// running/ok/error status, with the input JSON and (when settled)
// the tool result available behind the toggle. claude streams the
// input as partial_json deltas, so inputRaw may be incomplete or
// not parseable until the matching block_stop arrives — we display
// it verbatim and let the user judge.
function ToolUseRender({ block }: { block: ToolUseBlock }) {
    const [expanded, setExpanded] = React.useState(false);
    const settled = block.result !== undefined;
    const isError = block.result?.isError === true;
    const status = !settled ? 'running' : isError ? 'error' : 'ok';
    return (
        <div className={`tool-use status-${status}`}>
            <button
                type="button"
                className="tool-summary"
                onClick={() => setExpanded((e) => !e)}
                aria-expanded={expanded}
            >
                <span className="tool-toggle">{expanded ? '▾' : '▸'}</span>
                <span className="tool-name">{block.name}</span>
                <span className={`tool-status status-${status}`}>{status}</span>
            </button>
            {expanded && (
                <div className="tool-detail">
                    <div className="tool-section">
                        <div className="tool-label">input</div>
                        <pre>{block.inputRaw.trim() || '{}'}</pre>
                    </div>
                    {block.result && (
                        <div className="tool-section">
                            <div className="tool-label">
                                {isError ? 'error' : 'result'}
                            </div>
                            <pre>{block.result.content}</pre>
                        </div>
                    )}
                </div>
            )}
        </div>
    );
}

// Extended-thinking output. Hidden by default — the assistant's
// final answer is what the user came for; the chain-of-thought is
// available behind a toggle for power users.
function ThinkingRender({ block }: { block: ThinkingBlock }) {
    const [expanded, setExpanded] = React.useState(false);
    if (!block.text.trim()) return null;
    return (
        <div className="thinking">
            <button
                type="button"
                className="thinking-summary"
                onClick={() => setExpanded((e) => !e)}
                aria-expanded={expanded}
            >
                <span className="tool-toggle">{expanded ? '▾' : '▸'}</span>
                <span>thinking</span>
                <span className="tool-status muted">
                    {block.text.length.toLocaleString()} chars
                </span>
            </button>
            {expanded && <div className="thinking-text">{block.text}</div>}
        </div>
    );
}

// Citations for the chunks the retriever surfaced for this turn.
// Collapsed by default so the answer stays prominent; clicking
// expands the list, and clicking a row opens the file at the
// chunk's start line. This is the RAG-transparency feature: the
// user can immediately see *what* claude saw and audit whether
// retrieval picked the right code.
function SourcesFooter({
    sources,
    onOpen,
}: {
    sources: SourceChunk[];
    onOpen(path: string, line: number): void;
}) {
    const [expanded, setExpanded] = React.useState(false);
    return (
        <div className="sources">
            <button
                type="button"
                className="sources-summary"
                onClick={() => setExpanded((e) => !e)}
                aria-expanded={expanded}
            >
                <span className="tool-toggle">{expanded ? '▾' : '▸'}</span>
                <span>
                    {sources.length} source{sources.length === 1 ? '' : 's'}
                </span>
            </button>
            {expanded && (
                <ul className="sources-list">
                    {sources.map((s, i) => (
                        <li key={i}>
                            <button
                                type="button"
                                className="source-item"
                                onClick={() => onOpen(s.file, s.startLine)}
                                title={`${s.file}:${s.startLine}-${s.endLine}`}
                            >
                                <span className="source-symbol">
                                    {s.symbol || '<anonymous>'}
                                </span>
                                <span className="source-loc">
                                    {s.file}:{s.startLine}
                                </span>
                                {s.kind && (
                                    <span className="source-kind">{s.kind}</span>
                                )}
                            </button>
                        </li>
                    ))}
                </ul>
            )}
        </div>
    );
}

// Per-turn usage footer: token counts, cost, wall-clock duration.
// All fields are optional — claude omits cost when running on
// API-key auth, omits duration_ms in some configurations, etc.
function UsageFooter({ usage }: { usage: UsageData }) {
    const parts: string[] = [];
    if (typeof usage.inputTokens === 'number') {
        parts.push(`${usage.inputTokens.toLocaleString()} in`);
    }
    if (typeof usage.outputTokens === 'number') {
        parts.push(`${usage.outputTokens.toLocaleString()} out`);
    }
    if (typeof usage.cacheReadInputTokens === 'number' && usage.cacheReadInputTokens > 0) {
        parts.push(`${usage.cacheReadInputTokens.toLocaleString()} cached`);
    }
    if (typeof usage.costUsd === 'number') {
        parts.push(`$${usage.costUsd.toFixed(4)}`);
    }
    if (typeof usage.durationMs === 'number') {
        parts.push(`${(usage.durationMs / 1000).toFixed(1)}s`);
    }
    if (parts.length === 0) return null;
    return <div className="usage">{parts.join(' · ')}</div>;
}
