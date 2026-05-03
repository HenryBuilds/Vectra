# Changelog

All notable changes to Vectra are recorded in this file. Format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project
follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

The version number lives in the top-level `VERSION` file; CMake reads it
at configure time and the VS Code extension's `package.json` is gated
against it by `extension/vscode/scripts/check-version.js`. Bump VERSION,
mirror the package.json `version`, then tag.

## [Unreleased]

## [0.6.0] – 2026-05-03

This release is a large structural step: it adds the first end-to-end
in-chat permission flow, raises supported-language coverage from 8 to 33,
turns GPU support into a one-liner, and locks the project's release
plumbing in (single VERSION file, bundled LICENSE, CUDA CI gate).

### Added

#### CLI / `vectra ask`

- `--permission-mode {default | acceptEdits | plan | bypassPermissions}`
  exposes Claude Code's permission picker as a first-class option, with
  project-config and VS Code Setting fall-throughs. Default `acceptEdits`
  so `claude -p` no longer hangs on a permission prompt that has no UI to
  answer in.
- A two-rule "VECTRA INVARIANTS" header is injected into the user prompt
  when context chunks are present: TOOL ORDER (always call `Read` before
  `Edit`) and SCOPE (only modify files the user explicitly named).

#### Languages

- Tree-sitter coverage expanded from 8 to **33 languages**:
  Java, Ruby, C#, Bash, Kotlin, PHP, Markdown, JSON, YAML, TOML,
  Dockerfile, HCL/Terraform, Make, Scala, Lua, HTML, CSS, SQL, Dart,
  Elixir, Haskell, Clojure, R, Zig, OCaml — joining the previous
  C / C++ / Python / JavaScript / TypeScript / TSX / Rust / Go.
  Each language ships chunks / symbols / imports queries plus a fixture
  that the parse-validation suite exercises.
- New parse-validation test suite (`tests/core/parse_validation_test.cpp`)
  proves the chunks query yields ≥ 1 chunk and that all queries compile
  cleanly for every registered language. 264 assertions across 33
  language fixtures.

#### Build

- **GPU auto-detect at configure time**: macOS → Metal; Linux/Windows →
  CUDA → ROCm/HIP → Vulkan → CPU. New `VECTRA_AUTO_GPU` option (default
  `ON`) gates the probe; explicit `-DVECTRA_GPU_*=ON` skips it.
- Status banner now ends with `GPU backend : <Metal|CUDA|HIP/ROCm|
  Vulkan|cpu-only>` so the active backend survives the configure
  scrollback.
- Six new GPU-aware presets:
  `linux-clang-cuda-release`, `linux-gcc-cuda-release`,
  `windows-msvc-cuda-release`, `macos-clang-metal-release`,
  `linux-clang-rocm-release`, `linux-clang-vulkan-release`. The CUDA
  ones inherit a hidden `_gpu-cuda-multiarch` mixin pinning
  `CMAKE_CUDA_ARCHITECTURES=75-virtual;80-virtual;86-real;89-real;120a-real`
  (Turing → Blackwell) for redistributable binaries.
- New CI job `Linux · GCC · CUDA · compile-only` builds the
  CUDA preset against a real `nvcc` (CUDA Toolkit 12.8 via
  `Jimver/cuda-toolkit`); GitHub-hosted runners have no GPU so the run
  step is skipped, but compile-only catches the most common
  GPU-build-bricht regressions.

#### Extension

- **Per-edit in-chat approval flow**:
  - bundled MCP server (`scripts/mcp-permission-server.js`) plus a
    localhost HTTP `PermissionBridge` that the chat panel listens to;
  - tool-aware modal that previews diffs for Edit / Write, shows risk
    pills (`risky` / `caution` / `low risk`) for Bash with pattern flags
    (recursive delete, sudo, pipe-to-shell, destructive git, network,
    package change …), and falls back to a JSON dump for unknown tools;
  - tool-aware Approve label (`Apply edit` / `Run command` / `Write
    file`);
  - 90 s auto-deny timeout with an elapsed-seconds counter in the modal
    header so the user knows there's a deadline;
  - lifecycle logging into the **Vectra** output channel — every hop
    (`[bridge] incoming` → `[chat] forwarding` → `[chat] decision` →
    `[bridge] resolve` → `[bridge] flushing`) so a stuck approval is
    one log inspection away from being diagnosed.
- **Mode picker** in the chat-panel toolbar (`mode: settings` /
  `ask before edits` / `auto-edit` / `plan only` / `bypass permissions`)
  with the same labels as Claude Code's IDE-mode dropdown. Per-message
  override; persists across sends.
- **Inline tool-use renderers**: Edit / MultiEdit / Write blocks render
  a colored diff with a clickable file path right in the chat (mirrors
  Claude Code's IDE diff). Bash blocks show the command in a fenced
  code block with optional description and the captured output below.
- **Hallucination warning**: settled assistant turns that claim an edit
  ("geändert / aktualisiert / done / changed / …") but have no
  successful Edit / Write / MultiEdit tool_use surface a yellow
  advisory banner so the user verifies the file before relying on the
  answer.
- **Auto-reindex on file save** via a debounced FileSystemWatcher.
  Default 2 s quiet period, settable via `vectra.autoIndexDebounceMs`.
  Disabled with `vectra.autoIndex: false`.
- **`indexLock` mutex** serialises every `vectra index` invocation —
  manual `Vectra: Index workspace` and the auto-indexer share one
  FIFO so they never write the `.vectra/` SQLite database concurrently.
- **Embedding-model picker** in the empty state (`Change model &
  re-index…`) plus a top-level command `Vectra: Re-index with model…`.
  Auto-pulls the chosen model with `vectra model pull` before
  re-indexing so the user does not have to chase a "model not cached"
  error and re-run.
- **Workspace settings** for `vectra.indexModel`
  (`qwen3-embed-0.6b` / `4b` / `8b` / symbol-only) and `vectra.reranker`
  (`qwen3-rerank-0.6b` / off). Forwarded to both `vectra index` and
  `vectra ask` so retrieval matches what the index was built against.
- **Empty-state config card**: shows the active embedder + reranker
  with a one-click link to switch.
- **`scripts/check-version.js`** gates `vsce package` against drift
  between the top-level VERSION file and `extension/vscode/package.json`
  so a release tag never produces a VSIX whose version disagrees with
  the CLI binary's `--version`.
- **`scripts/sync-license.js`** mirrors the repo-root LICENSE into the
  extension dir at package time so the VSIX ships an `LICENSE.txt`
  entry — Marketplace-required and removes the `WARNING LICENSE not
  found` line that vsce was emitting on every package step.

#### Tests

- `tests/cli/cli_paths_test.cpp` covers 12 new edge cases for
  `find_project_root` (trailing slashes, dot-segments, both markers in
  the same dir, `.git` regular files for submodule worktrees,
  symlink resolution, filesystem-root termination, empty paths) plus
  `[windows]`-tagged long-path and `[apple]`-tagged APFS
  case-insensitivity tests.

### Changed

- `vectra ask` now defaults `--permission-mode acceptEdits` (down from
  no flag); the CLI no longer hangs on `claude -p`'s missing
  permission UI. Override with `-DVECTRA_AUTO_GPU=OFF` (CPU-only) or
  any explicit `--permission-mode` value.
- The user-prompt header for context chunks switched from a soft
  "verify before editing" hint to the explicit two-rule "VECTRA
  INVARIANTS" block. The earlier `--append-system-prompt` experiment
  was reverted in `bc69bcc` because the long multi-line value broke
  cmd.exe shell quoting on Windows; the rules now travel through the
  prompt tempfile instead.
- The MCP permission tool returns Claude Code's documented response
  shape (`{behavior: "allow"|"deny", message?}`) instead of the
  `{decision, reason}` shape the original spec write-up suggested —
  the wrong shape left Edit hanging in `running` after the user
  clicked Approve.

### Fixed

- macOS / APFS case-insensitivity edge case in
  `find_project_root` is now exercised in the test suite.
- Permission modal no longer renders "<unknown file>" with an empty
  diff when claude ships a renamed `tool_input` field — the renderer
  tries snake_case + camelCase + alias spellings (`file_path` /
  `filePath` / `path`) and falls through to a raw JSON dump if none
  match.
- Permission modal layout: actions (Approve / Deny) are pinned at the
  bottom of the modal via flex-column with `max-height: 50vh` so a
  long diff or expanded raw-input no longer pushes them off-screen.
- Auto-indexer no longer races with the manual `Vectra: Index workspace`
  command thanks to the shared `indexLock`.

### Project / Release

- New top-level `VERSION` file is the **single source of truth** for
  version strings. CMake `project(...)` reads it via `file(READ)`;
  `extension/vscode/package.json` is checked against it on every
  package step.
- `LICENSE` (Apache-2.0) preserved at repo root and mirrored into the
  VSIX at package time. The header copyright (`Copyright 2026 Vectra
  Contributors`) now matches the LICENSE attribution.
- This `CHANGELOG.md` introduced; future releases append a new
  `## [x.y.z] – YYYY-MM-DD` block above this one.

[Unreleased]: https://github.com/HenryBuilds/Vectra/compare/v0.6.0...HEAD
[0.6.0]: https://github.com/HenryBuilds/Vectra/releases/tag/v0.6.0
