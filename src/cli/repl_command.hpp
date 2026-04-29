// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra` REPL — interactive coding-agent loop.
//
// Each turn:
//   1. Read a prompt from the user.
//   2. Stream the LLM's reply to the terminal as tokens arrive.
//   3. Parse the reply for a unified diff. No diff = pure prose,
//      keep going.
//   4. Render the proposed patch with colored adds/removes.
//   5. Ask for approval. On 'y' the patch lands via vectra::exec::
//      apply_patch; on 'n' it is discarded; on 'q' the loop exits.
//
// The patch loop's self-healing retry (apply -> test -> re-prompt
// on failure) lands in a later commit. For now apply is single-
// shot — the user judges success themselves.

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>

#include "vectra/agent/llm_backend.hpp"

namespace vectra::cli {

struct ReplOptions {
    // Working tree the agent is allowed to modify. Defaults to
    // current_path() when wired up from the CLI.
    std::filesystem::path repo_root;

    // ANSI color output for the diff renderer and prompts.
    bool use_color = true;

    // Optional system-prompt override. Empty means "use the built-in
    // coding-agent prompt".
    std::string system_prompt;

    // Maximum number of user/assistant turn pairs to keep in the
    // conversation history. The system prompt is always retained.
    // 0 means "unbounded" — fine for short sessions, but long ones
    // will eventually overflow the model's context window.
    int history_limit = 0;
};

// Run the REPL against caller-supplied streams and a caller-owned
// backend. This is the form unit tests use; the production entry
// point is a thin wrapper that loads config, opens the backend, and
// delegates here.
[[nodiscard]] int run_repl(std::istream& in,
                           std::ostream& out,
                           agent::LlmBackend& backend,
                           const ReplOptions& opts);

}  // namespace vectra::cli
