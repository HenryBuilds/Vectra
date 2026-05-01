// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Typed wrapper around the VS Code webview API. Only used inside the
// webview — never imported by host code.

import type { Inbound, Outbound, PersistedState } from './types';

interface VsCodeApi {
    postMessage(message: Outbound): void;
    getState(): PersistedState | undefined;
    setState(state: PersistedState): void;
}

declare function acquireVsCodeApi(): VsCodeApi;

// acquireVsCodeApi may only be called once per webview lifetime, so
// we cache the handle module-level and re-export accessors.
const vscode: VsCodeApi = acquireVsCodeApi();

export function postMessage(message: Outbound): void {
    vscode.postMessage(message);
}

export function getState(): PersistedState | undefined {
    return vscode.getState();
}

export function setState(state: PersistedState): void {
    vscode.setState(state);
}

export type { Inbound, Outbound, PersistedState };
