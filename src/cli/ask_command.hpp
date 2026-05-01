// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra ask "<task>"` — RAG-driven dispatch to Claude Code.
//
// `ask` is the catch-all verb for "talk to the agent": bug fixes,
// explanations, refactors, new features, code review questions,
// anything you would type into Claude Code yourself. Bare-word
// `vectra "<task>"` (no subcommand) is rewritten to `vectra ask`
// before parsing so the verb is optional in the common case.
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

struct AskOptions {
    // The task description sent to Claude. Multiple positional words
    // are joined with spaces, so both `vectra ask "rename Foo"` and
    // `vectra ask rename Foo` produce the same prompt.
    std::vector<std::string> task_words;

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

    // Forwarded after `claude -p` as `--model <name>`. Empty means
    // "let claude pick its default model". Common values: "sonnet",
    // "opus", "haiku", or a full model id like "claude-opus-4-5".
    std::string claude_model;

    // Forwarded as `--effort <value>` so callers can dial Claude's
    // thinking budget without typing through --claude-arg. Same values
    // claude accepts ("low" / "medium" / "high"). Empty means "leave
    // claude on its default".
    std::string claude_effort;

    // Forwarded after the model/effort flags. Useful for anything the
    // dedicated knobs don't cover: --max-turns N, --allowedTools, ...
    std::vector<std::string> claude_extra_args;

    // Conversation continuity. When non-empty, forwarded to
    // `claude -p` so multi-turn callers (the VS Code chat panel)
    // get a single Claude session instead of N disconnected one-shots.
    //   --session-id <uuid>  → claude --session-id <uuid>
    //                          (assigns a UUID; first turn of a chat)
    //   --resume <uuid>      → claude --resume <uuid>
    //                          (continues an existing session; follow-up turns)
    // The two are mutually exclusive at parse time. Format: any
    // string claude accepts (it requires a valid UUID for
    // --session-id; we don't second-guess that here).
    std::string session_id;
    std::string resume_session;

    // When true, print the composed prompt to stdout and exit
    // without spawning claude. Useful for inspecting the context
    // the retriever surfaced for a task.
    bool print_prompt = false;

    // When true, suppress per-stage retrieval timing output on
    // stderr. The default surfaces stage names + millisecond
    // counts so the user can see where the wall-clock time is
    // going (model load, embedding, RRF, rerank).
    bool quiet = false;
};

[[nodiscard]] int run_ask(const AskOptions& opts);

}  // namespace vectra::cli
