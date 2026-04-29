// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra fix "<task>"` — one-shot self-healing fix.
//
// Loads config, opens the backend, detects the project's test
// adapter, and drives vectra::exec::run_patch_loop with a one-line
// task description. Each iteration shows the proposed diff (gated
// behind y/n unless --yes), applies, runs the tests, and on a fail
// hands the failure excerpt back to the model. After a clean run
// (or when --no-tests is set) the file changes stay; otherwise the
// loop's rollback restores the working tree.
//
// This is the non-interactive sibling of `vectra repl`. Both share
// the same agent-backend, diff-renderer, approval-prompt, and patch
// loop building blocks; the CLI just decides which UX they wear.

#pragma once

#include <filesystem>
#include <string>

namespace vectra::cli {

struct FixCommandOptions {
    // Required: the task description sent to the model.
    std::string task;

    // Project tree to modify. Auto-detected when empty.
    std::filesystem::path repo_root;

    // Config file. Defaults to <repo>/.vectra/config.toml.
    std::filesystem::path config_path;

    // Adapter manifest directory. Defaults walk through repo,
    // install, and exe-relative locations (see cli_paths.cpp).
    std::filesystem::path adapters_dir;

    // Self-healing budget. The patch loop runs at most this many
    // iterations before it gives up and rolls back. Coerced to 1
    // when no test adapter is available.
    int max_iterations = 3;

    // Skip the y/n approval prompt — every proposed patch is
    // applied. Useful for scripts and CI; the user takes
    // responsibility for the model's output.
    bool auto_approve = false;

    // Apply without running tests, even when an adapter is
    // available. Effectively forces single-shot mode.
    bool no_tests = false;

    // ANSI color override; the final decision pairs with TTY
    // detection inside run_fix_command.
    bool no_color = false;
};

// Returns a process-style exit code: 0 on a passing run, 1 on
// give-up / abort / no-patch / setup error.
[[nodiscard]] int run_fix_command(const FixCommandOptions& opts);

}  // namespace vectra::cli
