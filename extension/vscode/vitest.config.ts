// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Vitest configuration for the extension's host-side modules.
//
// What we test here is the *pure* logic — pathPolicy, allowList,
// commandPrefix, the JSON shape of permission requests — anything
// that does not need a live VS Code instance. End-to-end coverage
// (chatProvider spawning subprocesses, autoIndexer reacting to
// FileSystemWatcher events) belongs in @vscode/test-electron and
// is intentionally not wired here yet.
//
// Files under test import `vscode` only via narrow surfaces
// (Memento for AllowList) and the test files mock those by hand.
// Avoiding a global vscode mock keeps the unit tests fast and
// keeps the test surface honest — if a module starts pulling in
// vscode APIs that need real wiring, the test break loud.

import { defineConfig } from 'vitest/config';

export default defineConfig({
    test: {
        include: ['src/**/*.test.ts'],
        environment: 'node',
        globals: false,
        passWithNoTests: false,
        clearMocks: true,
    },
});
