#!/usr/bin/env node
// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Pre-publish gate that fails the package step when
// extension/vscode/package.json's `version` has drifted from the
// top-level VERSION file. CMakeLists.txt reads VERSION at configure
// time, so as long as this script keeps the extension manifest in
// lockstep all three release artifacts (CLI binary, VSIX, GitHub
// Release tag) describe the same build.

'use strict';

const fs = require('fs');
const path = require('path');

// Paths are resolved relative to this script so it does not matter
// which directory `npm run` was launched from.
const repoRoot = path.resolve(__dirname, '..', '..', '..');
const versionFile = path.join(repoRoot, 'VERSION');
const manifest = path.join(__dirname, '..', 'package.json');

let canonical;
try {
    canonical = fs.readFileSync(versionFile, 'utf8').trim();
} catch (err) {
    console.error(`check-version: cannot read ${versionFile}: ${err.message}`);
    process.exit(1);
}

let pkg;
try {
    pkg = JSON.parse(fs.readFileSync(manifest, 'utf8'));
} catch (err) {
    console.error(`check-version: cannot parse ${manifest}: ${err.message}`);
    process.exit(1);
}

if (pkg.version !== canonical) {
    console.error(
        `check-version: drift detected\n` +
            `  VERSION file:   ${canonical}\n` +
            `  package.json:   ${pkg.version}\n\n` +
            `Update extension/vscode/package.json's "version" to match the\n` +
            `top-level VERSION file (or vice versa) before packaging.`,
    );
    process.exit(1);
}

console.log(`check-version: ok (${canonical})`);
