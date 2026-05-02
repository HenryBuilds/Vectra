// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as React from 'react';

import * as host from './vscode';
import type {
    ChatMessage,
    MessageBlock,
    SourceChunk,
    ThinkingBlock,
    ToolUseBlock,
    UsageData,
} from './types';

// Loose JSON parse for streamed tool_use input. Claude streams
// `inputRaw` as partial_json deltas, so the string may not be valid
// JSON yet during the running phase. We return null until it parses
// cleanly; renderers fall back to the generic "still streaming"
// view in that case.
function tryParseJson(raw: string): Record<string, unknown> | null {
    const t = raw.trim();
    if (!t) return null;
    try {
        const v = JSON.parse(t);
        return typeof v === 'object' && v !== null ? (v as Record<string, unknown>) : null;
    } catch {
        return null;
    }
}

function pickString(o: Record<string, unknown>, ...keys: string[]): string | null {
    for (const k of keys) {
        const v = o[k];
        if (typeof v === 'string') return v;
    }
    return null;
}

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

// Detect the "claude claimed an edit but never called Edit/Write"
// hallucination pattern. Triggers only on settled assistant turns
// (streaming === false): we look at the rendered text for German /
// English "I changed it" phrasings and check whether a successful
// Edit / Write / MultiEdit tool_use exists in the same turn. False
// positives are acceptable — the warning is advisory, the user
// always has the file path in their head — and false negatives are
// fine too (claude phrases "done" in many ways). Goal is just
// "catch the most common case", not "linguistic completeness".
const EDIT_CLAIM_RE =
    /\b(edited|updated|changed|modified|applied|geändert|aktualisiert|geupdated|erledigt|done|saved|wrote|created|patched)\b/i;

function detectsEditClaim(blocks: MessageBlock[]): boolean {
    for (const b of blocks) {
        if (b.kind === 'text' && EDIT_CLAIM_RE.test(b.text)) return true;
    }
    return false;
}

function hasSuccessfulEditTool(blocks: MessageBlock[]): boolean {
    for (const b of blocks) {
        if (b.kind !== 'tool_use') continue;
        if (b.name !== 'Edit' && b.name !== 'MultiEdit' && b.name !== 'Write') continue;
        if (b.result === undefined) continue; // still running
        if (b.result.isError === true) continue;
        return true;
    }
    return false;
}

export function Message({ message, streaming, onAction, onOpenFile }: MessageProps) {
    const className = `message ${message.role}${streaming ? ' streaming' : ''}`;
    const showHallucinationWarning =
        !streaming &&
        message.role === 'assistant' &&
        detectsEditClaim(message.blocks) &&
        !hasSuccessfulEditTool(message.blocks);
    return (
        <div className={className}>
            <div className="role">{ROLE_LABELS[message.role]}</div>
            {message.meta && <div className="meta">{message.meta}</div>}
            {message.blocks.map((block, i) => (
                <BlockRenderer key={i} block={block} />
            ))}
            {showHallucinationWarning && (
                <div className="hallucination-warning" role="alert">
                    <strong>Heads up:</strong> Vectra didn't observe a successful
                    Edit / Write tool call in this turn — claude may be claiming a
                    change it didn't actually make. Verify the file before relying on
                    the answer.
                </div>
            )}
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

// Tool-use dispatcher. File-modifying tools render as inline diff
// blocks with file links; Bash renders as a styled command block;
// everything else falls back to the generic collapsible-JSON view.
// All renderers share the same status pill (running / ok / error)
// at the top so the user can scan a long turn quickly.
function ToolUseRender({ block }: { block: ToolUseBlock }) {
    if (block.name === 'Edit' || block.name === 'MultiEdit' || block.name === 'Write') {
        return <EditToolRender block={block} />;
    }
    if (block.name === 'Bash') {
        return <BashToolRender block={block} />;
    }
    return <GenericToolUseRender block={block} />;
}

function toolStatus(block: ToolUseBlock): {
    status: 'running' | 'ok' | 'error';
    isError: boolean;
} {
    const settled = block.result !== undefined;
    const isError = block.result?.isError === true;
    return {
        status: !settled ? 'running' : isError ? 'error' : 'ok',
        isError,
    };
}

// File link inside a chat tool-use block. Posts an openFile to the
// host so the user can open the affected file in a side editor
// while reading the diff.
function ToolFileLink({ path, line }: { path: string; line: number }) {
    const click = () => host.postMessage({ type: 'openFile', path, line });
    return (
        <button
            type="button"
            className="tool-file-link"
            onClick={click}
            title="Open file"
        >
            {path}
        </button>
    );
}

// Render Edit / MultiEdit / Write tool-uses inline as a coloured
// diff. Mirrors how Claude Code's IDE extension surfaces the
// pending change so the user can read the modification straight
// from the chat without expanding any toggles.
//
// While the tool is still streaming (inputRaw not yet valid JSON)
// we fall back to a "preparing edit…" placeholder. Once parsed,
// the diff appears with the file link, deleted line in red, added
// line in green, and any tool_result / error after the diff.
function EditToolRender({ block }: { block: ToolUseBlock }) {
    const { status, isError } = toolStatus(block);
    const parsed = tryParseJson(block.inputRaw);

    const file = parsed ? pickString(parsed, 'file_path', 'filePath', 'path') : null;
    const oldStr = parsed ? pickString(parsed, 'old_string', 'oldString') : null;
    const newStr = parsed
        ? pickString(parsed, 'new_string', 'newString', 'content')
        : null;

    const heading =
        block.name === 'Write'
            ? 'Write file'
            : block.name === 'MultiEdit'
              ? 'MultiEdit'
              : 'Edit';

    return (
        <div className={`tool-edit status-${status}`}>
            <div className="tool-edit-header">
                <span className="tool-edit-name">{heading}</span>
                <span className={`tool-status status-${status}`}>{status}</span>
            </div>
            {file !== null && <ToolFileLink path={file} line={1} />}
            {parsed === null && (
                <div className="tool-edit-pending">preparing edit…</div>
            )}
            {oldStr !== null && (
                <div className="tool-diff">
                    <span className="tool-diff-label">−</span>
                    <pre className="tool-diff-old">{oldStr}</pre>
                </div>
            )}
            {newStr !== null && (
                <div className="tool-diff">
                    <span className="tool-diff-label">+</span>
                    <pre className="tool-diff-new">{newStr}</pre>
                </div>
            )}
            {block.result && isError && (
                <div className="tool-edit-error">{block.result.content}</div>
            )}
        </div>
    );
}

// Render Bash tool-uses with the command in a fenced code block,
// optional description, and the captured output (or error) below
// once the tool settles.
function BashToolRender({ block }: { block: ToolUseBlock }) {
    const { status, isError } = toolStatus(block);
    const parsed = tryParseJson(block.inputRaw);
    const cmd = parsed ? pickString(parsed, 'command', 'cmd') : null;
    const desc = parsed ? pickString(parsed, 'description', 'desc') : null;

    return (
        <div className={`tool-bash status-${status}`}>
            <div className="tool-bash-header">
                <span className="tool-bash-name">Bash</span>
                <span className={`tool-status status-${status}`}>{status}</span>
            </div>
            {desc !== null && desc.trim().length > 0 && (
                <div className="tool-bash-desc">{desc}</div>
            )}
            {cmd !== null ? (
                <pre className="tool-bash-cmd">{cmd}</pre>
            ) : (
                <div className="tool-edit-pending">preparing command…</div>
            )}
            {block.result && (
                <pre className={`tool-bash-output${isError ? ' is-error' : ''}`}>
                    {block.result.content}
                </pre>
            )}
        </div>
    );
}

// Generic collapsible tool_use view for anything that isn't a
// dedicated specialisation: tool name + status, raw input + result
// behind a toggle. Same shape as the original ToolUseRender.
function GenericToolUseRender({ block }: { block: ToolUseBlock }) {
    const [expanded, setExpanded] = React.useState(false);
    const { status, isError } = toolStatus(block);
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
