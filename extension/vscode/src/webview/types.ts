// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// PostMessage protocol shared with chatProvider.ts (host side).
// Kept duplicated rather than imported across the host/webview
// boundary because the two TypeScript projects target different
// runtimes (Node vs browser).
//
// The protocol is centred on Anthropic's stream-json event shape:
// claude streams partial messages as a sequence of (block_start →
// many block_deltas → block_stop) per content block, plus one
// tool_result event per tool call and one usage event at the end.
// See chatProvider.ts for the parser that turns claude's NDJSON
// stdout into these typed events.

export type Role = 'user' | 'assistant' | 'error';

// One-shot action a message can offer the user. Clicking the button
// posts an ActionOutbound back to the host, which executes the
// corresponding VS Code command.
export interface MessageAction {
    label: string;
    commandId: string;
}

// ---- Content blocks ---------------------------------------------
//
// An assistant turn is a sequence of typed blocks. claude streams
// them by index (0, 1, 2, …) and they render in that order. User
// and error messages are modelled as a single text block so the
// rendering code does not need a special-case path.

export interface TextBlock {
    kind: 'text';
    text: string;
}

export interface ToolResultData {
    content: string;
    isError: boolean;
}

// claude streams tool_use input as a series of partial_json deltas
// that concatenate into a JSON string. Until block_stop arrives the
// raw form may not yet parse; callers should treat inputRaw as
// best-effort and tolerate JSON.parse throwing during streaming.
export interface ToolUseBlock {
    kind: 'tool_use';
    toolUseId: string;
    name: string;
    inputRaw: string;
    result?: ToolResultData;
}

export interface ThinkingBlock {
    kind: 'thinking';
    text: string;
}

export type MessageBlock = TextBlock | ToolUseBlock | ThinkingBlock;

export interface UsageData {
    inputTokens?: number;
    outputTokens?: number;
    cacheReadInputTokens?: number;
    cacheCreationInputTokens?: number;
    costUsd?: number;
    durationMs?: number;
}

// One chunk surfaced by retrieval and embedded into claude's
// prompt. Rendered under each assistant turn as a clickable
// citation: the file path resolves relative to the active
// workspace folder and clicking jumps to startLine.
export interface SourceChunk {
    file: string;
    startLine: number;
    endLine: number;
    symbol: string;
    kind: string;
}

export interface ChatMessage {
    id: string;
    role: Role;
    blocks: MessageBlock[];
    // Free-form retrieval-pipeline output streamed off vectra's
    // stderr (model name, per-stage timings, chunk counts). Rendered
    // as a collapsible meta section before the assistant text.
    meta: string;
    actions?: MessageAction[];
    usage?: UsageData;
    sources?: SourceChunk[];
}

// ---- Outgoing (webview -> host) ---------------------------------

export interface SendOutbound {
    type: 'send';
    id: string;
    text: string;
    model?: string;
    effort?: string;
    topK?: number;
    // Empty / undefined means "fall back to the vectra.permissionMode
    // workspace setting"; a non-empty value overrides the setting for
    // this single send only.
    permissionMode?: string;
}

export interface CancelOutbound {
    type: 'cancel';
    id: string;
}

export interface NewChatOutbound {
    type: 'newChat';
}

export interface ReadyOutbound {
    type: 'ready';
}

export interface ActionOutbound {
    type: 'action';
    commandId: string;
}

export interface OpenFileOutbound {
    type: 'openFile';
    path: string;
    line: number;
}

// Webview asks the host to persist the current session. Sent
// after every settled assistant turn (on `done`) and on explicit
// user actions like "+ New chat". The host stamps updatedAt and
// writes <storage>/sessions/<id>.json.
export interface SaveSessionOutbound {
    type: 'saveSession';
    title: string;
    messages: ChatMessage[];
}

// Webview decision for a PermissionRequestInbound. Echoes the
// requestId so the host can settle the HTTP response held open in
// the permission bridge. `reason` is optional; when omitted the
// host substitutes a generic "user approved / denied" string.
export interface PermissionResponseOutbound {
    type: 'permissionResponse';
    requestId: string;
    decision: 'allow' | 'deny';
    reason?: string;
}

export type Outbound =
    | SendOutbound
    | CancelOutbound
    | NewChatOutbound
    | ReadyOutbound
    | ActionOutbound
    | OpenFileOutbound
    | SaveSessionOutbound
    | PermissionResponseOutbound;

// ---- Incoming (host -> webview) ---------------------------------

// Begin an assistant turn. id matches the SendOutbound that
// triggered it; activeId in App.tsx tracks the in-flight turn.
export interface StartedInbound {
    type: 'started';
    id: string;
}

// Free-form text streamed off vectra's stderr. Today this carries
// the retrieval-pipeline summary; rendered as the "meta" line
// inside the assistant bubble.
export interface MetaInbound {
    type: 'meta';
    id: string;
    text: string;
}

// claude opened a new content block. The block's slot in the
// message's blocks[] array is reserved at the given blockIndex
// (which mirrors Anthropic's index numbering, 0..N-1 per turn).
export interface BlockStartInbound {
    type: 'block_start';
    id: string;
    blockIndex: number;
    block:
        | { kind: 'text' }
        | { kind: 'tool_use'; toolUseId: string; name: string }
        | { kind: 'thinking' };
}

// Append content to the block at blockIndex. The delta type must
// match the block's kind (text → text, tool_use → tool_input,
// thinking → thinking).
export interface BlockDeltaInbound {
    type: 'block_delta';
    id: string;
    blockIndex: number;
    delta:
        | { kind: 'text'; text: string }
        | { kind: 'tool_input'; partialJson: string }
        | { kind: 'thinking'; text: string };
}

// claude finished writing the block. Cosmetic for now; the renderer
// could use it to stop showing a streaming caret.
export interface BlockStopInbound {
    type: 'block_stop';
    id: string;
    blockIndex: number;
}

// A tool claude invoked has finished. The matching tool_use block
// is identified by toolUseId across all assistant blocks of the
// turn (since claude returns tool_results as a separate "user"
// message in the protocol, but visually they belong with the
// preceding tool_use).
export interface ToolResultInbound {
    type: 'tool_result';
    id: string;
    toolUseId: string;
    content: string;
    isError: boolean;
}

// Final usage / cost summary at the end of the turn.
export interface UsageInbound {
    type: 'usage';
    id: string;
    usage: UsageData;
}

// Retrieved source chunks. vectra emits one of these per turn
// before claude starts streaming, so the Sources footer fills in
// while the answer is still composing.
export interface SourcesInbound {
    type: 'sources';
    id: string;
    sources: SourceChunk[];
}

// Legacy plain-text fallback. claude with --output-format=stream-json
// should never emit non-JSON to stdout, but if vectra encounters
// pre-stream-json output (older claude binary, errors before claude
// starts) we surface it as a text block so the user is not left
// with a blank turn.
export interface ChunkInbound {
    type: 'chunk';
    id: string;
    text: string;
}

export interface DoneInbound {
    type: 'done';
    id: string;
    exitCode: number;
}

export interface ErrorInbound {
    type: 'error';
    id: string;
    message: string;
    actions?: MessageAction[];
}

export interface ConfigInbound {
    type: 'config';
    binary: string;
    defaultModel: string;
    defaultEffort: string;
    defaultTopK: number;
    // True iff <workspace>/.vectra/index.db exists. The empty
    // state hides the "Index this workspace" CTA when an index is
    // already present, which removes a recurring confusion source
    // ("vectra is asking me to index again even though I did").
    indexExists: boolean;
}

export interface NewChatInbound {
    type: 'newChat';
}

// Host pushes a session into the webview, either right after
// startup (the most recent session for this workspace) or after
// the user picks one from the history QuickPick. Replaces the
// current chat completely; the webview must drop any in-memory
// turn and adopt this session's messages verbatim.
//
// `session` is null for "start fresh" (newChat after a save).
export interface SessionLoadedInbound {
    type: 'sessionLoaded';
    session: { id: string; title: string; messages: ChatMessage[] } | null;
}

// In-flight approval shape. Same fields the host receives from the
// HTTP bridge; the webview's modal renders directly off this struct.
export interface PermissionRequest {
    requestId: string;
    toolName: string;
    toolInput: unknown;
    toolUseId: string;
}

// Forwarded from the permission-bridge HTTP server when claude wants
// to invoke a tool that requires approval. The webview shows a modal
// and replies with a PermissionResponseOutbound. requestId is opaque
// — the host uses it to look up the awaiting HTTP response; the
// webview just echoes it back.
export interface PermissionRequestInbound extends PermissionRequest {
    type: 'permissionRequest';
}

export type Inbound =
    | StartedInbound
    | MetaInbound
    | BlockStartInbound
    | BlockDeltaInbound
    | BlockStopInbound
    | ToolResultInbound
    | UsageInbound
    | SourcesInbound
    | ChunkInbound
    | DoneInbound
    | ErrorInbound
    | ConfigInbound
    | NewChatInbound
    | SessionLoadedInbound
    | PermissionRequestInbound;

export interface PersistedState {
    history: ChatMessage[];
    model: string;
    effort: string;
    permissionMode: string;
}
