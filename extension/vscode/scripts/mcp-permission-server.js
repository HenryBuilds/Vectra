#!/usr/bin/env node
// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Minimal MCP server that exposes a single `request_permission` tool
// for Claude Code's `--permission-prompt-tool` flag. Speaks JSON-RPC
// 2.0 over stdio per the Model Context Protocol; we hand-roll the
// transport to keep this script dependency-free so the extension can
// ship it as a single bundled file without dragging an npm install
// into the user's environment.
//
// Two operating modes, picked via env var:
//
//   VECTRA_MCP_AUTO_APPROVE=1
//       Decision is always {"decision":"allow","reason":"vectra auto"}.
//       Used by the throwaway prototype and by tests; verifies that
//       Claude actually calls our tool before each edit before we
//       wire up any UI. Default off.
//
//   VECTRA_MCP_IPC_PATH=<named-pipe-or-socket>
//       Connect to that endpoint, forward each request, await the
//       reply, return it. The VS Code extension hosts the endpoint
//       and shows the approval modal. (Wired in a follow-up commit;
//       this file currently implements the auto-approve path only.)
//
// Wire format we receive from Claude (via JSON-RPC method
// `tools/call` with `params.name === "request_permission"`):
//
//   { tool_name: "Edit",
//     tool_input: { file_path: "...", old_string: "...", ... },
//     tool_use_id: "toolu_…" }
//
// What we must return as the tool's text content (Claude reads the
// inner JSON out of the MCP `content[0].text` field):
//
//   { decision: "allow" | "deny",
//     reason?:  "string shown to claude",
//     updatedInput?: { ... overrides for the tool args ... } }

'use strict';

const http = require('http');
const { URL } = require('url');

const PROTOCOL_VERSION = '2024-11-05';
const SERVER_INFO = { name: 'vectra-permissions', version: '0.1.0' };

const TOOL_DEFINITION = {
    name: 'request_permission',
    description:
        'Approval gate for tool use. Vectra calls this whenever claude wants ' +
        'to invoke a permission-gated tool (Edit, Write, Bash, …). Returns ' +
        '{decision: "allow"|"deny"} as JSON inside content[0].text.',
    inputSchema: {
        type: 'object',
        properties: {
            tool_name: { type: 'string' },
            tool_input: { type: 'object' },
            tool_use_id: { type: 'string' },
        },
        required: ['tool_name', 'tool_input', 'tool_use_id'],
    },
};

// Stream JSON-RPC frames over stdio. Each request / response is a
// single JSON line (claude uses LSP-style stdio with newline-delimited
// JSON for MCP, not Content-Length framing).
function send(message) {
    process.stdout.write(JSON.stringify(message) + '\n');
}

function jsonResult(id, result) {
    send({ jsonrpc: '2.0', id, result });
}

function jsonError(id, code, message) {
    send({ jsonrpc: '2.0', id, error: { code, message } });
}

function postToBridge(bridgeUrl, token, body) {
    return new Promise((resolve, reject) => {
        let parsed;
        try {
            parsed = new URL('/approve', bridgeUrl);
        } catch (err) {
            reject(new Error(`bad VECTRA_BRIDGE_URL: ${err.message ?? err}`));
            return;
        }
        const payload = Buffer.from(JSON.stringify(body), 'utf8');
        const req = http.request(
            {
                hostname: parsed.hostname,
                port: parsed.port,
                path: parsed.pathname,
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                    'Content-Length': payload.length,
                    Authorization: `Bearer ${token}`,
                },
            },
            (res) => {
                const chunks = [];
                res.on('data', (c) => chunks.push(c));
                res.on('end', () => {
                    const text = Buffer.concat(chunks).toString('utf8');
                    if (res.statusCode !== 200) {
                        reject(
                            new Error(
                                `bridge returned ${res.statusCode}: ${text.slice(0, 200)}`,
                            ),
                        );
                        return;
                    }
                    try {
                        resolve(JSON.parse(text));
                    } catch (err) {
                        reject(new Error(`bridge returned non-JSON: ${err.message ?? err}`));
                    }
                });
            },
        );
        req.on('error', reject);
        req.write(payload);
        req.end();
    });
}

// Translate our internal {decision, reason} shape into the wire
// format claude actually expects: {behavior: "allow"|"deny", message?,
// updatedInput?}. Keeping the translation in one place means the
// chatProvider / bridge never has to know about claude's specific
// field names — they speak `decision` end-to-end internally.
function asClaudeVerdict(decision, reason) {
    if (decision === 'allow') {
        return { behavior: 'allow' };
    }
    return { behavior: 'deny', message: reason || 'denied' };
}

async function decide(toolName, toolInput, toolUseId) {
    if (process.env.VECTRA_MCP_AUTO_APPROVE === '1') {
        return asClaudeVerdict(
            'allow',
            'vectra auto-approve (VECTRA_MCP_AUTO_APPROVE=1)',
        );
    }

    const bridgeUrl = process.env.VECTRA_BRIDGE_URL;
    const bridgeToken = process.env.VECTRA_BRIDGE_TOKEN;
    if (!bridgeUrl || !bridgeToken) {
        // Fail closed: deny when no transport is wired up so a user
        // never silently approves edits because the env propagation
        // broke. The message string lands in claude's tool result.
        return asClaudeVerdict(
            'deny',
            'vectra: VECTRA_BRIDGE_URL / VECTRA_BRIDGE_TOKEN not set; ' +
                'the chat panel is the only thing that can approve edits.',
        );
    }

    try {
        const response = await postToBridge(bridgeUrl, bridgeToken, {
            tool_name: toolName,
            tool_input: toolInput,
            tool_use_id: toolUseId,
        });
        // The bridge speaks {decision, reason}; translate to claude's
        // {behavior, message?} on the way out. Anything malformed
        // becomes a deny so a future bridge revision can't silently
        // green-light edits.
        if (response && (response.decision === 'allow' || response.decision === 'deny')) {
            return asClaudeVerdict(response.decision, response.reason);
        }
        return asClaudeVerdict(
            'deny',
            `vectra: bridge returned an unrecognised body (${JSON.stringify(response).slice(0, 200)})`,
        );
    } catch (err) {
        return asClaudeVerdict(
            'deny',
            `vectra bridge error: ${err.message ?? String(err)}`,
        );
    }
}

async function handleRequest(req) {
    const { id, method, params } = req;

    switch (method) {
        case 'initialize':
            jsonResult(id, {
                protocolVersion: PROTOCOL_VERSION,
                capabilities: { tools: {} },
                serverInfo: SERVER_INFO,
            });
            return;

        case 'tools/list':
            jsonResult(id, { tools: [TOOL_DEFINITION] });
            return;

        case 'tools/call': {
            if (!params || params.name !== 'request_permission') {
                jsonError(id, -32601, `unknown tool: ${params?.name ?? '<none>'}`);
                return;
            }
            const args = params.arguments ?? {};
            // Diagnostic: dump the raw arguments object so we can
            // confirm the field names claude actually sends. Stderr
            // is forwarded into Vectra's output channel verbatim.
            try {
                process.stderr.write(
                    `vectra-mcp: tools/call args=${JSON.stringify(args).slice(0, 1000)}\n`,
                );
            } catch {
                // Best-effort; never let logging break the call.
            }
            // Be lenient about field names: the agent's spec said
            // {tool_name, tool_input, tool_use_id} but real claude
            // builds may emit camelCase or rename `tool_input` to
            // `input`. Try a few before falling back to empties.
            const toolName =
                (typeof args.tool_name === 'string' && args.tool_name) ||
                (typeof args.toolName === 'string' && args.toolName) ||
                '';
            const toolInput =
                (typeof args.tool_input === 'object' && args.tool_input) ||
                (typeof args.toolInput === 'object' && args.toolInput) ||
                (typeof args.input === 'object' && args.input) ||
                {};
            const toolUseId =
                (typeof args.tool_use_id === 'string' && args.tool_use_id) ||
                (typeof args.toolUseId === 'string' && args.toolUseId) ||
                '';
            const decision = await decide(toolName, toolInput, toolUseId);
            jsonResult(id, {
                content: [{ type: 'text', text: JSON.stringify(decision) }],
            });
            return;
        }

        case 'notifications/initialized':
            // No reply expected for notifications.
            return;

        default:
            // Unknown method; return JSON-RPC method-not-found for
            // request-style messages (those that have an `id`).
            if (typeof id !== 'undefined') {
                jsonError(id, -32601, `method not implemented: ${method}`);
            }
    }
}

// Line-buffered stdin reader. claude writes one JSON message per line.
let buffer = '';
process.stdin.setEncoding('utf8');
process.stdin.on('data', (chunk) => {
    buffer += chunk;
    let nl;
    while ((nl = buffer.indexOf('\n')) !== -1) {
        const line = buffer.slice(0, nl).trim();
        buffer = buffer.slice(nl + 1);
        if (!line) continue;
        let req;
        try {
            req = JSON.parse(line);
        } catch (err) {
            // Malformed input: log to stderr (claude surfaces stderr
            // verbatim in its diagnostics) and skip.
            process.stderr.write(
                `vectra-mcp: ignoring malformed JSON line: ${err.message}\n`,
            );
            continue;
        }
        // Fire and forget. The handler writes its own response.
        handleRequest(req).catch((err) => {
            process.stderr.write(`vectra-mcp: handler crashed: ${err.stack ?? err}\n`);
            if (typeof req?.id !== 'undefined') {
                jsonError(req.id, -32603, `internal error: ${err.message ?? err}`);
            }
        });
    }
});

process.stdin.on('end', () => process.exit(0));
