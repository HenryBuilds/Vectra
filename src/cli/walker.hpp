// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Filesystem walker that yields source files for indexing.
//
// Walks a directory tree, prunes well-known build / VCS / cache
// directories, drops files that are too large or unreadable, and
// keeps only those whose extension is registered in the language
// registry. Internal to vectra-cli.

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
        // Directory names that are skipped entirely. Matched on the
        // last component (so "a/b/.git" is skipped, but a literal
        // file called ".git" inside a tracked directory is not).
        std::unordered_set<std::string> ignore_dirs = {
            ".git",   ".hg",     ".svn",    ".vectra", ".vectra-cache", "node_modules",
            "target",  // Rust / Java
            "build",  "out",     "dist",
            "bin",  // common output dir; revisit if it causes false negatives
            "obj",    ".cache",  ".venv",   "venv",    "__pycache__",
            "vendor",  // Go modules vendor dir
            ".idea",  ".vscode", ".gradle", ".tox",
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
