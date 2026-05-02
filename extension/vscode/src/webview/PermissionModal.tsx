// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Inline approval modal. Pinned to the bottom of the chat stack
// while at least one PermissionRequest is pending; multiple queue
// up FIFO and surface one at a time as the user works through them.
//
// The component is presentation-only: App.tsx owns the queue, posts
// the decision back to the host, and shifts the head off the queue
// when the host settles it.
//
// Tool-specific renderers live below. Each takes the loose
// `toolInput` bag and produces a focused preview — diff for
// Edit / Write, command + risk badges for Bash, etc. The dispatcher
// at the bottom picks one based on tool name; an unknown tool
// surfaces a generic JSON preview so the user can still judge the
// call.

import * as React from 'react';

import * as host from './vscode';
import type { PermissionRequest } from './types';

interface PermissionModalProps {
    request: PermissionRequest;
    onApprove(): void;
    onDeny(): void;
}

// ---------------------------------------------------------------------------
// Field plucking
// ---------------------------------------------------------------------------

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

function pickBool(o: Record<string, unknown>, ...keys: string[]): boolean | null {
    for (const k of keys) {
        const v = o[k];
        if (typeof v === 'boolean') return v;
    }
    return null;
}

// ---------------------------------------------------------------------------
// Bash risk classification
// ---------------------------------------------------------------------------

type Risk = 'low' | 'medium' | 'high';

interface BashRisk {
    level: Risk;
    flags: string[];
}

// Pattern-match a shell command for known dangerous shapes. The
// goal is not perfect detection — that is impossible without a real
// shell parser — but a useful "watch out" hint for the most common
// foot-guns. False negatives are acceptable; false positives cost
// the user one second of squinting at the command.
function classifyBash(cmd: string): BashRisk {
    const flags: string[] = [];
    let level: Risk = 'low';

    // Recursive / forced deletes.
    if (/\brm\s+(?:[^|;&]*\s)?-{1,2}[a-zA-Z]*[rRf]/.test(cmd)) {
        flags.push('recursive delete');
        level = 'high';
    }
    // Privilege escalation.
    if (/\bsudo\b/.test(cmd)) {
        flags.push('sudo');
        level = 'high';
    }
    // curl|sh / wget|sh — the classic "run untrusted code" pattern.
    if (/(?:curl|wget)[^|]*\|\s*(?:sudo\s+)?(?:sh|bash|zsh)\b/.test(cmd)) {
        flags.push('pipe-to-shell');
        level = 'high';
    }
    // Force-push / hard reset / clean -fdx — destructive git ops.
    if (/\bgit\s+(?:push\s+(?:--force|-f)\b|reset\s+--hard\b|clean\s+-[a-zA-Z]*[fdxX])/.test(cmd)) {
        flags.push('destructive git');
        level = 'high';
    }
    // Hooks bypass — silently skipping CI / commit gates is suspect.
    if (/--no-verify\b|--dangerously-skip-permissions\b/.test(cmd)) {
        flags.push('skips checks');
        level = level === 'high' ? 'high' : 'medium';
    }
    // Network outbound (curl / wget / nc) without obvious read-only
    // intent — could be exfiltration or just an API call. Flag, do
    // not block.
    if (/\b(?:curl|wget)\b/.test(cmd) && level === 'low') {
        flags.push('network');
        level = 'medium';
    }
    // Package manager mutations.
    if (
        /\b(?:npm|pnpm|yarn|bun|cargo|pip|gem)\s+(?:install|add|update|upgrade|remove|uninstall)\b/.test(
            cmd,
        ) &&
        level === 'low'
    ) {
        flags.push('package change');
        level = 'medium';
    }
    // Build / migration commands.
    if (/\b(?:make|cmake|ninja|gradle|sbt|cargo)\s+(?:build|test|install)\b/.test(cmd) && level === 'low') {
        flags.push('build');
        level = 'medium';
    }
    // Output redirection that overwrites files (>) — mild flag, no
    // escalation past medium.
    if (/[^>]>(?!>)\s*\S/.test(cmd) && level === 'low') {
        flags.push('overwrites file');
        level = 'medium';
    }

    return { level, flags };
}

function RiskBadge({ risk }: { risk: BashRisk }) {
    const label =
        risk.level === 'high' ? 'risky' : risk.level === 'medium' ? 'caution' : 'low risk';
    return (
        <span className={`permission-risk permission-risk-${risk.level}`} title={risk.flags.join(', ')}>
            {label}
            {risk.flags.length > 0 && <span className="permission-risk-flags">{risk.flags.join(' · ')}</span>}
        </span>
    );
}

// ---------------------------------------------------------------------------
// Per-tool previews
// ---------------------------------------------------------------------------

// Clickable filename. Posts an `openFile` to the host so the user
// can open the affected file in a side editor while reviewing the
// approval. Mirrors the SourceFooter's behaviour.
function FileLink({ path }: { path: string }) {
    const click = () => {
        host.postMessage({ type: 'openFile', path, line: 1 });
    };
    return (
        <button type="button" className="permission-file-link" onClick={click} title="Open file">
            {path}
        </button>
    );
}

interface PreviewProps {
    o: Record<string, unknown>;
}

function EditPreview({ o }: PreviewProps): React.ReactElement | null {
    const file = pickString(o, 'file_path', 'filePath', 'path');
    const oldStr = pickString(o, 'old_string', 'oldString');
    const newStr = pickString(o, 'new_string', 'newString', 'content');
    const replaceAll = pickBool(o, 'replace_all', 'replaceAll');

    if (file === null && oldStr === null && newStr === null) {
        return null;
    }

    return (
        <>
            {file !== null && <FileLink path={file} />}
            {replaceAll === true && (
                <div className="permission-meta">replace_all: every match in the file will change</div>
            )}
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

function WritePreview({ o }: PreviewProps): React.ReactElement | null {
    const file = pickString(o, 'file_path', 'filePath', 'path');
    const content = pickString(o, 'content', 'text');

    if (file === null && content === null) {
        return null;
    }

    // For freshly created or fully overwritten files, claude tends to
    // ship the entire content as a single string. Rendering 500
    // lines of source in the modal would push the actions off the
    // visible area; we cap the preview to the first ~40 lines and
    // surface the rest behind a "show full" toggle so the user can
    // still inspect everything before approving.
    const [expanded, setExpanded] = React.useState(false);
    const lines = content?.split('\n') ?? [];
    const truncated = !expanded && lines.length > 40;
    const visible = truncated ? lines.slice(0, 40).join('\n') : (content ?? '');

    return (
        <>
            {file !== null && <FileLink path={file} />}
            {content !== null && (
                <>
                    <pre className="permission-write-content">{visible}</pre>
                    {truncated && (
                        <button
                            type="button"
                            className="permission-toggle"
                            onClick={() => setExpanded(true)}
                        >
                            show {lines.length - 40} more lines
                        </button>
                    )}
                </>
            )}
        </>
    );
}

function BashPreview({ o }: PreviewProps): React.ReactElement | null {
    const cmd = pickString(o, 'command', 'cmd');
    const desc = pickString(o, 'description', 'desc');
    const cwd = pickString(o, 'cwd', 'working_directory', 'workingDirectory');

    if (cmd === null && desc === null) {
        return null;
    }

    const risk = cmd !== null ? classifyBash(cmd) : null;

    return (
        <>
            {desc !== null && desc.trim().length > 0 && (
                <div className="permission-desc">{desc}</div>
            )}
            {cmd !== null && (
                <div className="permission-bash">
                    <pre className="permission-bash-cmd">{cmd}</pre>
                    {risk !== null && <RiskBadge risk={risk} />}
                </div>
            )}
            {cwd !== null && <div className="permission-meta">cwd: {cwd}</div>}
        </>
    );
}

// ---------------------------------------------------------------------------
// Dispatcher + raw input fallback
// ---------------------------------------------------------------------------

function rawInputFoldout(o: unknown): React.ReactElement {
    return (
        <details className="permission-raw">
            <summary>raw input</summary>
            <pre className="permission-input">{JSON.stringify(o, null, 2)}</pre>
        </details>
    );
}

function renderPreview(toolName: string, toolInput: unknown): React.ReactNode {
    if (typeof toolInput !== 'object' || toolInput === null) {
        return <pre className="permission-input">{String(toolInput)}</pre>;
    }
    const o = toolInput as Record<string, unknown>;
    const raw = rawInputFoldout(o);

    let structured: React.ReactElement | null = null;
    if (toolName === 'Edit' || toolName === 'MultiEdit') {
        structured = <EditPreview o={o} />;
    } else if (toolName === 'Write') {
        structured = <WritePreview o={o} />;
    } else if (toolName === 'Bash') {
        structured = <BashPreview o={o} />;
    }

    if (structured !== null) {
        return (
            <>
                {structured}
                {raw}
            </>
        );
    }
    // Unknown tool / unrecognised fields: just show the JSON.
    return raw;
}

// Tool-aware Approve label. Subtle but useful — "Approve edit"
// reads better than "Approve" when the user has read the diff and
// is ready to apply it.
function approveLabel(toolName: string): string {
    switch (toolName) {
        case 'Edit':
        case 'MultiEdit':
            return 'Apply edit';
        case 'Write':
            return 'Write file';
        case 'Bash':
            return 'Run command';
        default:
            return 'Approve';
    }
}

// Age counter displayed in the modal header. The bridge auto-denies
// after 90s so the user knows there is a deadline; rendering the
// elapsed seconds plays the same role as Claude Code's own
// "waiting…" indicator and keeps the user from wondering whether
// the approval actually went through.
function useElapsedSeconds(): number {
    const [start] = React.useState(() => Date.now());
    const [now, setNow] = React.useState(start);
    React.useEffect(() => {
        const t = setInterval(() => setNow(Date.now()), 1000);
        return () => clearInterval(t);
    }, []);
    return Math.floor((now - start) / 1000);
}

export function PermissionModal({ request, onApprove, onDeny }: PermissionModalProps) {
    const seconds = useElapsedSeconds();
    return (
        <div className="permission-modal" role="dialog" aria-modal="true">
            <div className="permission-header">
                <span className="permission-tool">{request.toolName || 'tool'}</span>
                <span className="permission-prompt">requests permission</span>
                {seconds >= 5 && <span className="permission-age">{seconds}s</span>}
            </div>
            <div className="permission-body">
                {renderPreview(request.toolName, request.toolInput)}
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
                    {approveLabel(request.toolName)}
                </button>
            </div>
        </div>
    );
}
