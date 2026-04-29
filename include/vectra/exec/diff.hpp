// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Unified-diff data model and parser.
//
// LLMs emit patches in unified-diff format ("--- a/foo.cpp / +++
// b/foo.cpp / @@ -X,Y +A,B @@ ..."). This module parses that text
// into a structured Patch that vectra::exec::apply_patch() can act
// on. The parser is intentionally tolerant of LLM quirks:
//
//   - Prose before / after / between diff blocks is silently
//     skipped. Anything that does not look like a diff header is
//     treated as commentary.
//   - The optional "a/" and "b/" path prefixes that git-style diffs
//     carry are stripped automatically.
//   - "/dev/null" on the old-path side flags a new file; on the
//     new-path side, a deleted file. Other rename / mode-change
//     metadata is ignored — LLMs effectively never emit it for
//     code-edit prompts.
//   - The final newline-at-EOF marker ("\ No newline at end of
//     file") is recognized but does not change the apply behaviour
//     in v1; we always preserve the existing file's terminator.
//
// What we do NOT support and will raise on:
//   - Combined / 3-way diffs ("@@@ ... @@@"): rare and unsafe to
//     apply heuristically.
//   - Binary diffs ("Binary files ... differ"): we return an
//     error rather than silently dropping the change.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vectra::exec {

// One hunk inside a FileDiff. Line numbers follow the unified-diff
// convention: 1-indexed, inclusive on the start, exclusive on the
// end (so "@@ -10,5 ..." covers lines 10..14 of the old file).
struct Hunk {
    std::int32_t old_start = 0;
    std::int32_t old_count = 0;
    std::int32_t new_start = 0;
    std::int32_t new_count = 0;

    // Each entry retains its prefix character (' ', '+', or '-') so
    // the applier can tell context from additions / deletions
    // without re-parsing. A '\\' line denoting "no newline at end
    // of file" is also preserved; the applier consumes it but
    // emits no character for it.
    std::vector<std::string> lines;
};

struct FileDiff {
    // Repository-relative paths, "a/" / "b/" prefixes already
    // stripped. `old_path` is empty for a new-file diff;
    // `new_path` is empty for a deleted-file diff.
    std::string old_path;
    std::string new_path;

    bool is_new_file = false;
    bool is_deleted = false;

    std::vector<Hunk> hunks;
};

// A patch is the union of all file-level diffs the LLM produced in
// one response. Order is preserved so that downstream tooling can
// apply edits in the order the model presented them.
struct Patch {
    std::vector<FileDiff> files;

    [[nodiscard]] bool empty() const noexcept { return files.empty(); }
};

// Parse `text` into a Patch.
//
// Returns an empty Patch when no diff markers are present (so an
// LLM response that is pure prose round-trips to "no changes").
// Throws std::runtime_error when a diff block is recognized but
// malformed (e.g. mismatched hunk counts, unsupported combined
// diffs). The exception message points at the offending line so
// the user can surface it back to the model in a retry prompt.
[[nodiscard]] Patch parse_unified_diff(std::string_view text);

}  // namespace vectra::exec
