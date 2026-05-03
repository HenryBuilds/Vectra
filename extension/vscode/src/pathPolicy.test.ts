// Copyright 2026 Vectra Contributors. Apache-2.0.

import * as path from 'path';
import { describe, expect, it } from 'vitest';

import { checkToolPath, isInsideWorkspace } from './pathPolicy';

const WORKSPACE = process.platform === 'win32' ? 'C:\\work\\repo' : '/work/repo';

describe('isInsideWorkspace', () => {
    it('accepts a child path', () => {
        expect(isInsideWorkspace(path.join('src', 'foo.ts'), WORKSPACE).inside).toBe(true);
    });

    it('accepts the workspace root itself', () => {
        expect(isInsideWorkspace(WORKSPACE, WORKSPACE).inside).toBe(true);
    });

    it('accepts an absolute path that lives inside the root', () => {
        const abs = path.join(WORKSPACE, 'src', 'foo.ts');
        expect(isInsideWorkspace(abs, WORKSPACE).inside).toBe(true);
    });

    it('rejects a parent traversal', () => {
        expect(isInsideWorkspace('../escape.ts', WORKSPACE).inside).toBe(false);
    });

    it('rejects an absolute path outside the root', () => {
        const outside = process.platform === 'win32'
            ? 'C:\\Windows\\System32\\config\\SAM'
            : '/etc/passwd';
        expect(isInsideWorkspace(outside, WORKSPACE).inside).toBe(false);
    });

    it('rejects a sibling that shares a prefix', () => {
        // /work/repo vs /work/repository — repository must NOT be
        // counted as inside repo. path.relative would yield
        // "../repository", so this is the canary that the prefix
        // check is real and not a startsWith mistake.
        const sibling = process.platform === 'win32' ? 'C:\\work\\repository\\x.ts' : '/work/repository/x.ts';
        expect(isInsideWorkspace(sibling, WORKSPACE).inside).toBe(false);
    });

    it('rejects a different drive on Windows', () => {
        if (process.platform !== 'win32') return;
        expect(isInsideWorkspace('D:\\elsewhere\\foo.ts', WORKSPACE).inside).toBe(false);
    });

    it('collapses redundant traversals that stay inside', () => {
        const trick = path.join('src', '..', 'src', 'foo.ts');
        expect(isInsideWorkspace(trick, WORKSPACE).inside).toBe(true);
    });

    it('rejects a traversal that detours outside before returning', () => {
        // path.resolve normalises so this becomes the literal
        // parent, which is correctly rejected.
        const trick = path.join('src', '..', '..', 'escape.ts');
        expect(isInsideWorkspace(trick, WORKSPACE).inside).toBe(false);
    });
});

describe('checkToolPath', () => {
    it('passes through tools without a path field', () => {
        expect(checkToolPath('Bash', { command: 'ls' }, WORKSPACE)).toEqual({ kind: 'no-path' });
    });

    it('passes through tools with no input', () => {
        expect(checkToolPath('Edit', null, WORKSPACE)).toEqual({ kind: 'no-path' });
    });

    it('flags missing workspace for path-bearing tools', () => {
        expect(
            checkToolPath('Edit', { file_path: 'src/foo.ts' }, undefined),
        ).toEqual({ kind: 'no-workspace' });
    });

    it('allows an Edit on a workspace-relative path', () => {
        expect(
            checkToolPath('Edit', { file_path: 'src/foo.ts' }, WORKSPACE).kind,
        ).toBe('ok');
    });

    it('allows a Write under the workspace via absolute path', () => {
        const abs = path.join(WORKSPACE, 'src', 'new.ts');
        expect(checkToolPath('Write', { file_path: abs }, WORKSPACE).kind).toBe('ok');
    });

    it('allows a NotebookEdit under the workspace', () => {
        expect(
            checkToolPath('NotebookEdit', { notebook_path: 'data/foo.ipynb' }, WORKSPACE).kind,
        ).toBe('ok');
    });

    it('denies an Edit that walks above the workspace', () => {
        const verdict = checkToolPath('Edit', { file_path: '../escape.ts' }, WORKSPACE);
        expect(verdict.kind).toBe('deny');
    });

    it('denies an absolute path outside the workspace', () => {
        const outside = process.platform === 'win32'
            ? 'C:\\Users\\victim\\Documents\\secret.docx'
            : '/etc/passwd';
        const verdict = checkToolPath('Write', { file_path: outside }, WORKSPACE);
        expect(verdict.kind).toBe('deny');
        if (verdict.kind === 'deny') {
            expect(verdict.resolvedPath).toContain(outside.includes('passwd') ? 'passwd' : 'secret');
        }
    });

    it('accepts the camelCase filePath alias', () => {
        expect(
            checkToolPath('Edit', { filePath: 'src/foo.ts' }, WORKSPACE).kind,
        ).toBe('ok');
    });
});
