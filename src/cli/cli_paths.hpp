// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Path-discovery helpers shared by the CLI subcommands. None of these
// take user input directly — main.cpp parses argv and the subcommand
// callbacks call into these to turn flags into concrete filesystem
// locations (or to fall back to sensible defaults).

#pragma once

#include <filesystem>
#include <optional>

namespace vectra::cli {

// Walk up from `start` looking for a directory that contains either
// `.vectra` (our own marker) or `.git`. Returns the first match.
// Returns nullopt when no marker is found before the filesystem
// root — the caller decides whether to fall back to `start` or
// abort with a friendly error.
[[nodiscard]] std::optional<std::filesystem::path> find_project_root(
    const std::filesystem::path& start);

// Resolve the config-file path. When `override_path` is non-empty
// it wins; otherwise the canonical location is `<repo>/.vectra/
// config.toml`.
[[nodiscard]] std::filesystem::path resolve_config_path(const std::filesystem::path& repo_root,
                                                        const std::filesystem::path& override_path);

// Resolve the adapter-manifest directory. `--adapters` wins, then a
// dev-build's repo-local `adapters/`, then the install-side
// `<exe>/../share/vectra/adapters` and `<exe>/adapters`. Returns
// nullopt when none of those exist; callers running without test
// runners can still proceed.
[[nodiscard]] std::optional<std::filesystem::path> resolve_adapters_dir(
    const std::filesystem::path& repo_root, const std::filesystem::path& override_path);

// Absolute path of the running executable, or an empty path if the
// platform call fails. Used by adapter discovery.
[[nodiscard]] std::filesystem::path current_exe_path();

// Whether stdout is attached to a terminal. Used to gate ANSI color
// output: redirected stdout (file, pipe) gets clean text, a real
// console gets colors.
[[nodiscard]] bool stdout_is_tty() noexcept;

}  // namespace vectra::cli
