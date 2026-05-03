#!/usr/bin/env node
// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Pre-publish helper that copies the repo-root LICENSE file into
// extension/vscode/ so vsce includes it in the VSIX. The repo root
// is the single source of truth; this script only mirrors. Treat
// extension/vscode/LICENSE as a generated artifact — do not edit
// it directly, do not commit it to the repository.

'use strict';

const fs = require('fs');
const path = require('path');

const repoRoot = path.resolve(__dirname, '..', '..', '..');
const src = path.join(repoRoot, 'LICENSE');
const dst = path.join(__dirname, '..', 'LICENSE');

try {
    fs.copyFileSync(src, dst);
} catch (err) {
    console.error(`sync-license: copy failed: ${err.message ?? err}`);
    console.error(`  source:      ${src}`);
    console.error(`  destination: ${dst}`);
    process.exit(1);
}

console.log(`sync-license: copied ${src} → ${dst}`);
