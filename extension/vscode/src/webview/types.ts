// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// PostMessage protocol shared with chatProvider.ts (host side).
// Kept duplicated rather than imported across the host/webview
// boundary because the two TypeScript projects target different
// runtimes (Node vs browser).

export type Role = 'user' | 'assistant' | 'error';

// One-shot action a message can offer the user. Clicking the button
// posts an ActionOutbound back to the host, which executes the
// corresponding VS Code command. Keeps the chat actionable without
// breaking the streaming flow.
export interface MessageAction {
    label: string;
    commandId: string;
}

export interface ChatMessage {
    id: string;
    role: Role;
    body: string;
    meta: string;
    actions?: MessageAction[];
}

// ---- Outgoing (webview -> host) ---------------------------------

export interface SendOutbound {
    type: 'send';
    id: string;
    text: string;
    model?: string;
    effort?: string;
    topK?: number;
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

export type Outbound =
    | SendOutbound
    | CancelOutbound
    | NewChatOutbound
    | ReadyOutbound
    | ActionOutbound;

// ---- Incoming (host -> webview) ---------------------------------

export interface ChunkInbound {
    type: 'chunk';
    id: string;
    text: string;
}

export interface MetaInbound {
    type: 'meta';
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

export interface StartedInbound {
    type: 'started';
    id: string;
}

export interface ConfigInbound {
    type: 'config';
    binary: string;
    defaultModel: string;
    defaultEffort: string;
    defaultTopK: number;
}

export interface NewChatInbound {
    type: 'newChat';
}

export type Inbound =
    | ChunkInbound
    | MetaInbound
    | DoneInbound
    | ErrorInbound
    | StartedInbound
    | ConfigInbound
    | NewChatInbound;

export interface PersistedState {
    history: ChatMessage[];
    model: string;
    effort: string;
}
