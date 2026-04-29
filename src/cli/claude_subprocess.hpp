// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Bridge from Vectra to the Claude Code CLI. The new `vectra fix`
// pipeline retrieves relevant chunks from the local index, composes
// a prompt that includes the user's task and those chunks as
// labeled context, writes the prompt to a tempfile, and shells out
// to `claude -p < tempfile`. Claude Code itself authenticates,
// streams its reply, and edits files via its native tools — Vectra
// only sets the stage.
//
// This module owns three concerns:
//   1. compose_prompt — pure formatting of (task + chunks) into the
//      string we send to claude. Unit-tested.
//   2. TempFile — RAII tempfile so the prompt does not leak when
//      the command throws or the user CTRL-Cs.
//   3. run_claude — the actual subprocess: streams stdout to the
//      caller's stream and returns claude's exit code.

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <string_view>
#include <vector>

namespace vectra::cli {

// A single chunk surfaced by retrieval, ready to be embedded in the
// prompt. Mirrors the shape of retrieve::Hit but is free of the
// retrieval-specific bits the prompt does not need (score, kind
// enum, etc.).
struct ContextChunk {
    std::string file_path;  // repo-relative
    int start_line;         // 1-indexed inclusive
    int end_line;           // 1-indexed inclusive
    std::string symbol;     // may be empty
    std::string kind;       // "function", "class", etc.; may be empty
    std::string text;       // chunk body, verbatim from the index
};

struct PromptComposition {
    std::string task;
    std::vector<ContextChunk> context;
};

// Build the prompt that gets fed to `claude -p`. Plain-text task
// header, then one labeled block per chunk. Pure function — no IO.
[[nodiscard]] std::string compose_prompt(const PromptComposition& comp);

// RAII tempfile. The constructor reserves a unique path under the
// system temp dir; write() materializes it; the destructor unlinks
// it best-effort. Throws on filesystem errors during write().
class TempFile {
public:
    explicit TempFile(std::string_view label);
    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
    TempFile(TempFile&&) = delete;
    TempFile& operator=(TempFile&&) = delete;
    ~TempFile();

    void write(std::string_view content);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

struct ClaudeInvocation {
    // Path to a file containing the prompt. Will be redirected to
    // claude's stdin via shell redirection; subject to the temp
    // dir's filesystem permissions.
    std::filesystem::path prompt_file;

    // Override the binary name. Defaults to "claude" (PATH lookup).
    std::string claude_binary = "claude";

    // Flags forwarded to claude after `-p`. Common ones: --model,
    // --max-turns, --allowedTools / --disallowedTools.
    std::vector<std::string> extra_args;
};

// Spawn claude, stream its stdout to `out`. stderr is left
// connected to the parent process so the user sees diagnostic
// noise inline. Returns claude's exit code, or -1 if the spawn
// itself failed.
[[nodiscard]] int run_claude(const ClaudeInvocation& inv, std::ostream& out);

}  // namespace vectra::cli
