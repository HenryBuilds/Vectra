// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// In-process HTTP bridge between the bundled mcp-permission-server.js
// (spawned by claude per `--permission-prompt-tool`) and the chat
// webview that owns the approval UI.
//
// Flow per Edit / Write / Bash that claude wants to run:
//
//   claude          → spawns mcp-permission-server.js (stdio MCP)
//   mcp-server      → POST http://127.0.0.1:<port>/approve
//   PermissionBridge→ holds the HTTP response open
//                     emits onRequest() → chatProvider posts to webview
//   webview         → renders modal, user clicks Allow / Deny
//                     posts permissionResponse back
//   chatProvider    → calls bridge.resolve(requestId, decision)
//   PermissionBridge→ writes the JSON body to the held HTTP response
//   mcp-server      → returns the decision to claude (via MCP content)
//   claude          → executes Edit / aborts based on decision
//
// Localhost-only and gated by a per-activation random bearer token so
// other processes on the same machine cannot drive the bridge.

import * as crypto from 'crypto';
import * as http from 'http';
import * as vscode from 'vscode';

export type PermissionDecision = 'allow' | 'deny';

export interface PermissionRequest {
    requestId: string;
    toolName: string;
    toolInput: unknown;
    toolUseId: string;
}

export interface PermissionResponse {
    decision: PermissionDecision;
    reason?: string;
}

type Resolver = (response: PermissionResponse) => void;

export class PermissionBridge implements vscode.Disposable {
    private readonly server: http.Server;
    private readonly token: string;
    private readonly pending: Map<string, Resolver> = new Map();
    private port: number = 0;
    private nextRequestId = 0;
    private listener: ((req: PermissionRequest) => void) | undefined;

    constructor() {
        this.token = crypto.randomBytes(24).toString('hex');
        this.server = http.createServer((req, res) => this.handle(req, res));
    }

    /**
     * Start the HTTP listener on a random localhost port. Resolves
     * once the port is bound and known.
     */
    async start(): Promise<void> {
        await new Promise<void>((resolve, reject) => {
            this.server.once('error', reject);
            this.server.listen(0, '127.0.0.1', () => {
                this.server.removeListener('error', reject);
                const addr = this.server.address();
                if (typeof addr !== 'object' || addr === null) {
                    reject(new Error('PermissionBridge: failed to bind port'));
                    return;
                }
                this.port = addr.port;
                resolve();
            });
        });
    }

    /** http://127.0.0.1:<port> — empty until start() resolves. */
    get url(): string {
        return `http://127.0.0.1:${this.port}`;
    }

    /**
     * Bearer token the MCP-side script must include in its
     * `Authorization: Bearer <token>` header. Per-activation; never
     * persisted to disk.
     */
    get authToken(): string {
        return this.token;
    }

    /**
     * Register the chatProvider's callback. Replaces any prior
     * listener — only one chat panel ever owns the bridge at a time.
     */
    setListener(listener: (req: PermissionRequest) => void): void {
        this.listener = listener;
    }

    /**
     * Called by chatProvider once the webview has produced a
     * decision. No-ops if the request id is unknown (e.g. claude
     * already gave up and the modal was answered late).
     */
    resolve(requestId: string, response: PermissionResponse): void {
        const resolver = this.pending.get(requestId);
        if (!resolver) {
            return;
        }
        this.pending.delete(requestId);
        resolver(response);
    }

    /**
     * Force-deny every in-flight request. Called when a chat run is
     * cancelled — claude is going away anyway, but we resolve the
     * holds so the MCP server's HTTP awaiters do not leak.
     */
    denyAll(reason: string): void {
        for (const [, resolver] of this.pending) {
            resolver({ decision: 'deny', reason });
        }
        this.pending.clear();
    }

    dispose(): void {
        this.denyAll('vectra extension deactivating');
        this.server.close();
    }

    private handle(req: http.IncomingMessage, res: http.ServerResponse): void {
        // Hard 404 for everything but the one route — keeps the
        // attack surface small if some other localhost client probes.
        if (req.method !== 'POST' || req.url !== '/approve') {
            res.writeHead(404).end();
            return;
        }
        const auth = req.headers.authorization ?? '';
        if (auth !== `Bearer ${this.token}`) {
            res.writeHead(401).end();
            return;
        }

        const chunks: Buffer[] = [];
        req.on('data', (c: Buffer) => chunks.push(c));
        req.on('end', () => {
            let body: { tool_name?: string; tool_input?: unknown; tool_use_id?: string };
            try {
                body = JSON.parse(Buffer.concat(chunks).toString('utf8')) as typeof body;
            } catch {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(
                    JSON.stringify({ decision: 'deny', reason: 'malformed request body' }),
                );
                return;
            }

            if (!this.listener) {
                // No one is listening — surface a clear deny rather
                // than letting the MCP server's HTTP request stall.
                res.writeHead(503, { 'Content-Type': 'application/json' });
                res.end(
                    JSON.stringify({
                        decision: 'deny',
                        reason: 'vectra: no chat panel is registered to handle approvals',
                    }),
                );
                return;
            }

            const requestId = `r${this.nextRequestId++}-${Date.now().toString(36)}`;
            const request: PermissionRequest = {
                requestId,
                toolName: body.tool_name ?? '',
                toolInput: body.tool_input ?? {},
                toolUseId: body.tool_use_id ?? '',
            };

            // Stash the resolver so chatProvider can settle it once
            // the webview produces a decision.
            this.pending.set(requestId, (response) => {
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(response));
            });

            // If the request socket goes away — claude killed the
            // MCP server, the user cancelled the run — drop the
            // pending entry so the in-flight modal becomes a no-op
            // when the user finally clicks.
            req.on('close', () => {
                if (this.pending.has(requestId)) {
                    this.pending.delete(requestId);
                }
            });

            this.listener(request);
        });
    }
}
