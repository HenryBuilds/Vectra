# Vectra for VS Code

Thin wrapper around the [`vectra`](https://github.com/HenryBuilds/Vectra)
CLI. Lets you ask Vectra to retrieve relevant chunks from your local
index and dispatch the task to Claude Code without leaving the editor.

## Prerequisites

1. The `vectra` binary on your `PATH` (or set `vectra.binary` in
   settings to an absolute path).
2. Claude Code installed and logged in: `npm i -g @anthropic-ai/claude-code`
   followed by `claude login`.
3. An indexed workspace — run **Vectra: Index workspace** the first
   time you open a project.

## Commands

| Command                            | What it does                                                                 |
| ---------------------------------- | ---------------------------------------------------------------------------- |
| `Vectra: Ask about codebase`       | Prompts for a task, runs `vectra ask "<task>"`, streams output.              |
| `Vectra: Ask about selection`      | Right-click in the editor with a selection. Wraps the selection in the task. |
| `Vectra: Index workspace`          | Runs `vectra index .` against the workspace root.                            |

## Settings

| Setting              | Default    | Description                                                                                |
| -------------------- | ---------- | ------------------------------------------------------------------------------------------ |
| `vectra.binary`      | `vectra`   | Path to the `vectra` binary.                                                               |
| `vectra.claudeModel` | _(empty)_  | Forwarded as `--claude-model` (`haiku` / `sonnet` / `opus`).                               |
| `vectra.effort`      | _(empty)_  | Forwarded as `--effort` (`low` / `medium` / `high`).                                       |
| `vectra.topK`        | `0`        | Number of context chunks. `0` falls back to `.vectra/config.toml` or the built-in default. |

Per-project defaults live in `.vectra/config.toml`:

```toml
[retrieve]
model    = "qwen3-embed-0.6b"
reranker = "qwen3-rerank-0.6b"

[claude]
model  = "opus"
effort = "high"
```

## Build

```sh
cd extension/vscode
npm install
npm run compile
npm run package      # produces vectra-vscode-<version>.vsix
```

Install the produced `.vsix` via the VS Code command palette
(`Extensions: Install from VSIX…`) or:

```sh
code --install-extension vectra-vscode-0.0.1.vsix
```

Cursor, Windsurf, and other VS Code forks accept the same `.vsix`.
