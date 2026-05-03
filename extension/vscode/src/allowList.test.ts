// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as path from 'path';
import { describe, expect, it } from 'vitest';

import { AllowList, commandPrefix, commandPrefixMatches } from './allowList';

const WORKSPACE = process.platform === 'win32' ? 'C:\\work\\repo' : '/work/repo';

// Minimal in-memory stand-in for vscode.Memento. The real interface
// has more methods (keys, setKeysForSync) but AllowList only uses
// get/update so a two-method shim is enough.
class FakeMemento {
    private store = new Map<string, unknown>();
    get<T>(key: string, fallback?: T): T | undefined {
        return (this.store.has(key) ? (this.store.get(key) as T) : fallback);
    }
    async update(key: string, value: unknown): Promise<void> {
        this.store.set(key, value);
    }
    keys(): readonly string[] {
        return Array.from(this.store.keys());
    }
}

function fresh(): { memento: FakeMemento; list: AllowList } {
    const memento = new FakeMemento();
    return { memento, list: new AllowList(memento as unknown as import('vscode').Memento, WORKSPACE) };
}

describe('commandPrefix', () => {
    it('returns empty for empty input', () => {
        expect(commandPrefix('')).toBe('');
    });

    it('returns the single token', () => {
        expect(commandPrefix('ls')).toBe('ls');
    });

    it('returns the first two tokens by default', () => {
        expect(commandPrefix('npm test --verbose')).toBe('npm test');
    });

    it('collapses interior whitespace', () => {
        expect(commandPrefix('  git    status  ')).toBe('git status');
    });
});

describe('commandPrefixMatches', () => {
    it('matches an exact command', () => {
        expect(commandPrefixMatches('npm test', 'npm test')).toBe(true);
    });

    it('matches additional args after the prefix', () => {
        expect(commandPrefixMatches('npm test --verbose', 'npm test')).toBe(true);
    });

    it('rejects a different second token', () => {
        expect(commandPrefixMatches('npm install', 'npm test')).toBe(false);
    });

    it('rejects a longer pattern than the command', () => {
        expect(commandPrefixMatches('ls', 'ls -la')).toBe(false);
    });

    it('rejects an empty pattern', () => {
        expect(commandPrefixMatches('ls', '')).toBe(false);
    });
});

describe('AllowList', () => {
    it('starts empty', () => {
        const { list } = fresh();
        expect(list.list()).toEqual([]);
    });

    it('persists adds to the memento', async () => {
        const { memento, list } = fresh();
        await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
        expect(memento.get('vectra.allowList.v1')).toEqual([
            { toolName: 'Edit', pattern: 'src/foo.ts' },
        ]);
    });

    it('hydrates from existing memento data', async () => {
        const { memento } = fresh();
        await memento.update('vectra.allowList.v1', [
            { toolName: 'Bash', pattern: 'npm test' },
        ]);
        const list = new AllowList(memento as unknown as import('vscode').Memento, WORKSPACE);
        expect(list.list()).toEqual([{ toolName: 'Bash', pattern: 'npm test' }]);
    });

    it('drops malformed entries during hydration', async () => {
        const { memento } = fresh();
        await memento.update('vectra.allowList.v1', [
            { toolName: 'Edit', pattern: 'src/foo.ts' },
            'garbage',
            { toolName: 42, pattern: 'x' },
            null,
            { pattern: 'orphan' },
        ]);
        const list = new AllowList(memento as unknown as import('vscode').Memento, WORKSPACE);
        expect(list.list()).toEqual([{ toolName: 'Edit', pattern: 'src/foo.ts' }]);
    });

    it('de-dupes adds', async () => {
        const { list } = fresh();
        await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
        await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
        expect(list.list()).toHaveLength(1);
    });

    it('removes a stored entry', async () => {
        const { list } = fresh();
        await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
        await list.add({ toolName: 'Edit', pattern: 'src/bar.ts' });
        await list.remove({ toolName: 'Edit', pattern: 'src/foo.ts' });
        expect(list.list()).toEqual([{ toolName: 'Edit', pattern: 'src/bar.ts' }]);
    });

    it('clears all entries', async () => {
        const { list } = fresh();
        await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
        await list.add({ toolName: 'Bash', pattern: 'ls' });
        await list.clear();
        expect(list.list()).toEqual([]);
    });

    describe('matches()', () => {
        it('returns false for an unknown tool', () => {
            const { list } = fresh();
            expect(list.matches('Edit', { file_path: 'src/foo.ts' })).toBe(false);
        });

        it('matches an exact path entry on Edit', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
            expect(list.matches('Edit', { file_path: 'src/foo.ts' })).toBe(true);
        });

        it('matches the same path passed as absolute', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
            const abs = path.join(WORKSPACE, 'src', 'foo.ts');
            expect(list.matches('Edit', { file_path: abs })).toBe(true);
        });

        it('rejects a different path under the same tool', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
            expect(list.matches('Edit', { file_path: 'src/bar.ts' })).toBe(false);
        });

        it('does not cross tools', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Edit', pattern: 'src/foo.ts' });
            expect(list.matches('Write', { file_path: 'src/foo.ts' })).toBe(false);
        });

        it('matches a Bash command-prefix entry', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Bash', pattern: 'npm test' });
            expect(list.matches('Bash', { command: 'npm test --verbose' })).toBe(true);
            expect(list.matches('Bash', { command: 'npm install lodash' })).toBe(false);
        });

        it('star pattern matches any input on its tool', async () => {
            const { list } = fresh();
            await list.add({ toolName: 'Read', pattern: '*' });
            expect(list.matches('Read', { file_path: '/anywhere' })).toBe(true);
            expect(list.matches('Read', {})).toBe(true);
        });
    });

    describe('suggestPattern()', () => {
        it('suggests a workspace-relative path for Edit', () => {
            const { list } = fresh();
            const abs = path.join(WORKSPACE, 'src', 'foo.ts');
            expect(list.suggestPattern('Edit', { file_path: abs })).toBe('src/foo.ts');
        });

        it('suggests a path for Write', () => {
            const { list } = fresh();
            expect(list.suggestPattern('Write', { file_path: 'README.md' })).toBe('README.md');
        });

        it('suggests a command prefix for Bash', () => {
            const { list } = fresh();
            expect(list.suggestPattern('Bash', { command: 'cargo build --release' })).toBe(
                'cargo build',
            );
        });

        it('suggests * for unknown tools', () => {
            const { list } = fresh();
            expect(list.suggestPattern('Read', { file_path: 'whatever' })).toBe('*');
        });

        it('returns null for malformed input', () => {
            const { list } = fresh();
            expect(list.suggestPattern('Edit', {})).toBe(null);
            expect(list.suggestPattern('Bash', null)).toBe(null);
        });

        it('returns the absolute path when the file is outside the workspace', () => {
            const { list } = fresh();
            const outside = process.platform === 'win32' ? 'C:\\Windows\\notepad.exe' : '/etc/passwd';
            expect(list.suggestPattern('Edit', { file_path: outside })).toBe(outside);
        });
    });
});
