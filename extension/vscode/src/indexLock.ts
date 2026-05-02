// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Module-global mutex serializing every `vectra index` invocation
// the extension launches — both the manual `Vectra: Index workspace`
// command and the auto-indexer FileSystemWatcher path go through it.
//
// Without this, a manual click that happened while the auto-indexer
// was mid-run would spawn a second `vectra index` against the same
// .vectra/ SQLite database. SQLite's own write lock keeps that
// safe (no corruption), but the second writer stalls until the
// first commits and surfaces a confusing latency hiccup. Funneling
// every spawn through one lock keeps the spawn ordering FIFO and
// the user-visible behavior predictable.
//
// Usage:
//
//     const release = await indexLock.acquire();
//     try {
//         // run `vectra index ...` here
//     } finally {
//         release();
//     }

class IndexLock {
    private locked = false;
    private waiters: Array<() => void> = [];

    /**
     * Wait until the lock is free, take it, and return a release
     * callback the caller must invoke (typically in a `finally`).
     * Calls are served FIFO: the next waiter in line gets the lock
     * the moment the current holder releases.
     */
    async acquire(): Promise<() => void> {
        if (this.locked) {
            await new Promise<void>((resolve) => {
                this.waiters.push(resolve);
            });
        }
        this.locked = true;
        return () => this.release();
    }

    /**
     * True while the lock is held OR another caller is queued. The
     * manual command uses this to surface a "waiting for auto-
     * indexer" hint without having to peek at private state.
     */
    get busy(): boolean {
        return this.locked || this.waiters.length > 0;
    }

    private release(): void {
        const next = this.waiters.shift();
        if (next) {
            // Hand the lock straight to the next waiter — do not flip
            // `locked` to false, because that would let a third caller
            // skipping in via acquire() bypass the queue.
            next();
        } else {
            this.locked = false;
        }
    }
}

export const indexLock = new IndexLock();
