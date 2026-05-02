// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Disk-backed chat session store. Each Vectra chat session has a
// stable UUID (which doubles as claude's --session-id) and is
// persisted as a single JSON file under the extension's
// globalStorage area:
//
//   <globalStorage>/<workspace-hash>/sessions/<uuid>.json
//
// We hash the workspace folder path so two different projects can
// never see each other's history — opening the chat in repo A and
// then in repo B should give two separate session lists. Inside a
// single workspace the layout is flat: one file per session,
// listing is `readdir` + `stat`, no manifest to keep in sync.
//
// The shape of `messages` is opaque here on purpose. The webview
// owns the ChatMessage schema; the host just persists whatever
// blob it gets back via the `saveSession` postMessage.

import * as crypto from 'crypto';
import * as fs from 'fs/promises';
import * as path from 'path';
import * as vscode from 'vscode';

export interface SessionMetadata {
    id: string;
    title: string;
    createdAt: number;
    updatedAt: number;
}

export interface PersistedSession extends SessionMetadata {
    version: number;
    messages: unknown[];
}

const SCHEMA_VERSION = 1;

export class ChatStorage {
    constructor(private readonly storageRoot: vscode.Uri) {}

    // Hash the workspace folder so different projects never see
    // each other's history. SHA-256 truncated to 16 hex chars is
    // collision-safe at any realistic project count.
    private workspaceDir(workspaceUri: vscode.Uri): string {
        const hash = crypto
            .createHash('sha256')
            .update(workspaceUri.fsPath)
            .digest('hex')
            .slice(0, 16);
        return path.join(this.storageRoot.fsPath, hash);
    }

    private sessionsDir(workspaceUri: vscode.Uri): string {
        return path.join(this.workspaceDir(workspaceUri), 'sessions');
    }

    private sessionPath(workspaceUri: vscode.Uri, id: string): string {
        return path.join(this.sessionsDir(workspaceUri), `${id}.json`);
    }

    public async list(workspaceUri: vscode.Uri): Promise<SessionMetadata[]> {
        const dir = this.sessionsDir(workspaceUri);
        let files: string[];
        try {
            files = await fs.readdir(dir);
        } catch {
            return [];
        }
        const out: SessionMetadata[] = [];
        for (const f of files) {
            if (!f.endsWith('.json')) continue;
            try {
                const raw = await fs.readFile(path.join(dir, f), 'utf-8');
                const data = JSON.parse(raw) as Partial<PersistedSession>;
                if (typeof data.id !== 'string') continue;
                if (typeof data.title !== 'string') continue;
                if (typeof data.createdAt !== 'number') continue;
                if (typeof data.updatedAt !== 'number') continue;
                out.push({
                    id: data.id,
                    title: data.title,
                    createdAt: data.createdAt,
                    updatedAt: data.updatedAt,
                });
            } catch {
                // Skip corrupt files. We deliberately do not delete
                // them — leave them around for diagnosis.
            }
        }
        // Most-recently-updated first; that's what the user
        // overwhelmingly wants when picking from the history.
        out.sort((a, b) => b.updatedAt - a.updatedAt);
        return out;
    }

    public async load(
        workspaceUri: vscode.Uri,
        id: string,
    ): Promise<PersistedSession | null> {
        try {
            const raw = await fs.readFile(this.sessionPath(workspaceUri, id), 'utf-8');
            const data = JSON.parse(raw) as PersistedSession;
            return data;
        } catch {
            return null;
        }
    }

    public async save(workspaceUri: vscode.Uri, session: PersistedSession): Promise<void> {
        const dir = this.sessionsDir(workspaceUri);
        await fs.mkdir(dir, { recursive: true });
        // Write to a temp file then rename, so a crash mid-write
        // never leaves a half-corrupt JSON behind.
        const target = this.sessionPath(workspaceUri, session.id);
        const tmp = `${target}.tmp-${process.pid}-${Date.now()}`;
        const payload: PersistedSession = { ...session, version: SCHEMA_VERSION };
        await fs.writeFile(tmp, JSON.stringify(payload, null, 2), 'utf-8');
        await fs.rename(tmp, target);
    }

    public async remove(workspaceUri: vscode.Uri, id: string): Promise<void> {
        try {
            await fs.unlink(this.sessionPath(workspaceUri, id));
        } catch {
            // Already gone.
        }
    }
}

// Format a Unix-millis timestamp as a humane relative string for
// the QuickPick description column. Goes back to absolute date
// after a week so "32 days ago" doesn't get vague.
export function relativeTime(ts: number): string {
    const now = Date.now();
    const diff = Math.max(0, now - ts);
    const sec = Math.floor(diff / 1000);
    if (sec < 60) return 'just now';
    const min = Math.floor(sec / 60);
    if (min < 60) return `${min} minute${min === 1 ? '' : 's'} ago`;
    const hr = Math.floor(min / 60);
    if (hr < 24) return `${hr} hour${hr === 1 ? '' : 's'} ago`;
    const day = Math.floor(hr / 24);
    if (day < 7) return `${day} day${day === 1 ? '' : 's'} ago`;
    const d = new Date(ts);
    return d.toISOString().slice(0, 10);
}
