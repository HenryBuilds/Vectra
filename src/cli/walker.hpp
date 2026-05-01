// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Filesystem walker that yields source files for indexing.
//
// Two-mode operation:
//
//   1. **Git mode** (preferred). When `root` lives inside a git
//      working tree, the walker shells out to `git ls-files
//      --cached --others --exclude-standard` and uses the result
//      as the file set. This means project-specific .gitignore,
//      multi-level .gitignore, .git/info/exclude, and the user's
//      global gitignore are *all* honoured for free, with no
//      pattern-parsing code on our side. Files git tracks plus
//      untracked-but-not-ignored are kept; ignored and the .git
//      directory itself drop out.
//
//   2. **Filesystem fallback**. If `git` is not on PATH, the root
//      is not inside a repo, or the subprocess fails for any
//      reason, the walker falls back to a recursive directory
//      iterator with a small hardcoded skip list. This keeps
//      vectra usable on directories that are not git repos.
//
// Either mode then filters by language registry (extension known)
// and file size before returning. Internal to vectra-cli.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

namespace vectra::core {
class LanguageRegistry;
}

namespace vectra::cli {

class FileWalker {
public:
    struct Options {
        // Directory names that are skipped in the **filesystem
        // fallback path** (no git available / not in a repo).
        // Matched on the last component (so "a/b/.git" is skipped,
        // but a literal file called ".git" inside a tracked
        // directory is not).
        //
        // Kept deliberately small: framework-specific build outputs
        // (.next, .turbo, .svelte-kit, …) are not in this list
        // because (a) they are reliably in every modern project's
        // .gitignore so the git path skips them automatically, and
        // (b) maintaining a comprehensive list across an evolving
        // ecosystem is its own treadmill. The entries here are the
        // universal-skip set that should be ignored even if a
        // user's .gitignore is broken or missing.
        std::unordered_set<std::string> ignore_dirs = {
            // VCS metadata
            ".git",
            ".hg",
            ".svn",
            // Vectra state
            ".vectra",
            ".vectra-cache",
            // Always-noise dependency dirs
            "node_modules",
            "vendor",  // Go modules vendor dir
            "__pycache__",
            // Generic build outputs (covered by .gitignore in any
            // healthy project, kept as fallback-only safety net)
            "target",  // Rust / Java
            "build",
            "dist",
            "out",
        };

        // Files exceeding this size are skipped; very large source
        // files are typically generated and not worth embedding.
        std::size_t max_file_size_bytes = 5 * 1024 * 1024;

        // If true, follow symlinks during traversal. Off by default
        // because following symlinks risks descending into out-of-tree
        // directories or hitting cycles.
        bool follow_symlinks = false;
    };

    // Two overloads instead of `Options opts = Options{}`. Clang
    // refuses the default-arg form because evaluating Options{}
    // requires the in-class initializer for ignore_dirs (a brace-
    // initialized unordered_set) to be visible at the point where
    // the constructor signature is parsed — but the enclosing class
    // is not complete yet there. Splitting the default into a
    // separate constructor sidesteps the rule.
    FileWalker() noexcept;
    explicit FileWalker(Options opts) noexcept;

    // Walk `root` recursively and return the list of source-file
    // paths (absolute) whose extension is registered in `registry`
    // and that pass the filter checks. The returned vector is
    // sorted by path so output order is deterministic.
    [[nodiscard]] std::vector<std::filesystem::path> walk(
        const std::filesystem::path& root, const core::LanguageRegistry& registry) const;

    [[nodiscard]] const Options& options() const noexcept { return opts_; }

private:
    Options opts_;
};

}  // namespace vectra::cli
