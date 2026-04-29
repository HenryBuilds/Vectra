// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra fix "<task>"` — RAG-driven dispatch to Claude Code.
//
// What it does:
//   1. Find the project root and open the index DB (same convention
//      as `vectra index` / `vectra search`).
//   2. Run hybrid retrieval for the task — exactly the pipeline
//      `vectra search` uses, with the optional embedder and
//      reranker — and take the top-K chunks.
//   3. Compose a prompt: the task verbatim, followed by each chunk
//      labeled with its file path, line range, and symbol.
//   4. Spawn `claude -p` with that prompt on stdin and stream its
//      output to the terminal.
//
// Vectra deliberately does no editing of its own here. Claude Code
// already handles file reads / writes / tool use / approvals via
// its own UX; Vectra's contribution is the retrieval that gives
// Claude a head start on which files to look at.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace vectra::cli {

struct FixOptions {
    // The task description sent to Claude. Required.
    std::string task;

    // Project root. Auto-detected (walk up from CWD looking for
    // .vectra or .git) when empty.
    std::filesystem::path repo_root;

    // Override for the index DB. Defaults to <root>/.vectra/index.db.
    std::filesystem::path db;

    // Number of context chunks to surface. 0 means "fall back to
    // .vectra/config.toml's [retrieve].top_k, then to a built-in
    // default of 8". The CLI flag, when passed, overrides both.
    std::size_t k = 0;

    // Embedding-model registry name. Empty -> symbol-only retrieval.
    std::string model;

    // Cross-encoder reranker registry name. Empty -> no reranking.
    std::string reranker;

    // Override the claude binary (e.g. an absolute path or a wrapper
    // script). Defaults to "claude" on PATH.
    std::string claude_binary;

    // Forwarded after `claude -p`. Useful for `--model claude-opus`,
    // `--max-turns N`, `--allowedTools Edit,Bash`, etc.
    std::vector<std::string> claude_extra_args;

    // When true, print the composed prompt to stdout and exit
    // without spawning claude. Useful for inspecting the context
    // the retriever surfaced for a task.
    bool print_prompt = false;
};

[[nodiscard]] int run_fix(const FixOptions& opts);

}  // namespace vectra::cli
