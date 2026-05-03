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

// Hard upper bound on how long a single approval can sit on screen
// before we auto-deny it. Without this an unanswered modal pins the
// underlying claude run forever — claude never times out the MCP
// call from its end (or it times out so far in the future the user
// has long since written off the chat as "broken"). 90s is a
// compromise: long enough to read a moderate diff and decide, short
// enough that walking away from the desk recovers a usable panel.
const REQUEST_TIMEOUT_MS = 90_000;

export class PermissionBridge implements vscode.Disposable {
    private readonly server: http.Server;
    private readonly token: string;
    private readonly pending: Map<string, Resolver> = new Map();
    private readonly log: (msg: string) => void;
    private port: number = 0;
    private nextRequestId = 0;
    private listener: ((req: PermissionRequest) => void) | undefined;

    constructor(output?: vscode.OutputChannel) {
        this.token = crypto.randomBytes(24).toString('hex');
        this.server = http.createServer((req, res) => this.handle(req, res));
        // Lifecycle logger: every approval round-trip touches the
        // bridge twice (incoming HTTP from the MCP server + resolve()
        // from the chat panel). Writing both to the Vectra output
        // channel is the cheapest way to diagnose "Apply clicked but
        // nothing happened" issues — the chain is webview → host →
        // bridge → MCP → claude and any of those four hops can drop
        // a message silently.
        this.log = output ? (m) => output.appendLine(`[bridge] ${m}`) : () => {};
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
     * True once the listener has bound a real port and is ready to
     * serve approvals. Callers should check this before spawning a
     * subprocess that points at the bridge — otherwise claude will
     * call back to a port that nothing is listening on, and the
     * approval flow looks "stuck" to the user.
     */
    get isReady(): boolean {
        return this.port > 0;
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
            this.log(
                `resolve(${requestId}) — no resolver. ` +
                    `pending=[${[...this.pending.keys()].join(',')}]`,
            );
            return;
        }
        this.pending.delete(requestId);
        this.log(`resolve(${requestId}) decision=${response.decision}`);
        resolver(response);
    }

    /**
     * Force-deny every in-flight request. Called when a chat run is
     * cancelled — claude is going away anyway, but we resolve the
     * holds so the MCP server's HTTP awaiters do not leak.
     */
    denyAll(reason: string): void {
        if (this.pending.size > 0) {
            this.log(`denyAll: clearing ${this.pending.size} pending — ${reason}`);
        }
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
            this.log(
                `incoming approval ${requestId} tool=${request.toolName} useId=${request.toolUseId}`,
            );

            // Auto-deny timer: if no decision arrives within
            // REQUEST_TIMEOUT_MS we settle the request as a deny
            // with a clear message so the user knows why claude
            // gave up. Cleared by either resolve() or req.on('close').
            let timer: NodeJS.Timeout | undefined = setTimeout(() => {
                if (this.pending.has(requestId)) {
                    this.log(`timeout ${requestId} after ${REQUEST_TIMEOUT_MS}ms — auto-deny`);
                    this.resolve(requestId, {
                        decision: 'deny',
                        reason: `vectra: no decision after ${Math.round(REQUEST_TIMEOUT_MS / 1000)}s — auto-denied`,
                    });
                }
            }, REQUEST_TIMEOUT_MS);

            // Stash the resolver so chatProvider can settle it once
            // the webview produces a decision.
            this.pending.set(requestId, (response) => {
                if (timer !== undefined) {
                    clearTimeout(timer);
                    timer = undefined;
                }
                this.log(
                    `flushing HTTP response ${requestId} decision=${response.decision}`,
                );
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify(response));
            });

            // If the request socket goes away — claude killed the
            // MCP server, the user cancelled the run — drop the
            // pending entry so the in-flight modal becomes a no-op
            // when the user finally clicks.
            req.on('close', () => {
                if (timer !== undefined) {
                    clearTimeout(timer);
                    timer = undefined;
                }
                if (this.pending.has(requestId)) {
                    this.pending.delete(requestId);
                }
            });

            this.listener(request);
        });
    }
}
