// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "fix_command.hpp"

#include <fmt/format.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <system_error>

#include "vectra/agent/llm_backend.hpp"
#include "vectra/exec/patch_loop.hpp"
#include "vectra/exec/test_runner.hpp"

#include "approval.hpp"
#include "cli_paths.hpp"
#include "diff_render.hpp"

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] const char* outcome_label(exec::PatchLoopOutcome o) noexcept {
    switch (o) {
        case exec::PatchLoopOutcome::PassedFirstTry:
            return "ok";
        case exec::PatchLoopOutcome::PassedAfterRetry:
            return "ok";
        case exec::PatchLoopOutcome::GaveUp:
            return "failed";
        case exec::PatchLoopOutcome::Aborted:
            return "aborted";
        case exec::PatchLoopOutcome::NoPatch:
            return "no-patch";
    }
    return "unknown";
}

}  // namespace

int run_fix_command(const FixCommandOptions& cli) {
    if (cli.task.empty()) {
        fmt::print(stderr, "error: task is required\n");
        return 1;
    }

    // ---- repo root ---------------------------------------------------
    fs::path repo_root = cli.repo_root;
    if (repo_root.empty()) {
        if (auto found = find_project_root(fs::current_path()); found) {
            repo_root = std::move(*found);
        } else {
            fmt::print(stderr,
                       "error: no project root detected (looked for .vectra or .git).\n"
                       "       run from inside a project, or pass --root <path>.\n");
            return 1;
        }
    } else {
        std::error_code ec;
        if (!fs::is_directory(repo_root, ec)) {
            fmt::print(stderr, "error: --root is not a directory: {}\n", repo_root.string());
            return 1;
        }
    }

    // ---- config ------------------------------------------------------
    const auto config_path = resolve_config_path(repo_root, cli.config_path);
    agent::AgentConfig cfg;
    try {
        std::error_code ec;
        if (fs::exists(config_path, ec)) {
            cfg = agent::AgentConfig::from_toml(config_path);
        } else if (!cli.config_path.empty()) {
            fmt::print(stderr, "error: --config not found: {}\n", config_path.string());
            return 1;
        } else {
            cfg = agent::AgentConfig::with_defaults();
        }
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: invalid config {}: {}\n", config_path.string(), e.what());
        return 1;
    }

    // ---- backend -----------------------------------------------------
    std::unique_ptr<agent::LlmBackend> backend;
    try {
        backend = agent::open_backend(cfg);
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;
    }

    // ---- adapter -----------------------------------------------------
    std::optional<exec::TestAdapter> adapter;
    if (!cli.no_tests) {
        if (auto adapters_dir = resolve_adapters_dir(repo_root, cli.adapters_dir)) {
            try {
                const auto adapters = exec::load_adapters(*adapters_dir);
                if (auto picked = exec::select_adapter(adapters, repo_root)) {
                    adapter = std::move(*picked);
                }
            } catch (const std::exception& e) {
                fmt::print(stderr, "warning: adapter loading failed: {}\n", e.what());
            }
        } else if (!cli.adapters_dir.empty()) {
            fmt::print(
                stderr, "error: --adapters not a directory: {}\n", cli.adapters_dir.string());
            return 1;
        }
    }

    // ---- color & banner ---------------------------------------------
    const bool use_color = !cli.no_color && stdout_is_tty();

    fmt::print("project: {}\n", repo_root.string());
    fmt::print("model:   {} ({})\n", cfg.model, cfg.backend);
    if (adapter) {
        fmt::print("tests:   {} (max iterations {})\n", adapter->name, cli.max_iterations);
    } else if (cli.no_tests) {
        fmt::print("tests:   skipped (--no-tests)\n");
    } else {
        fmt::print("tests:   no adapter detected; applying without verification\n");
    }
    fmt::print("task:    {}\n\n", cli.task);

    // ---- loop wiring -------------------------------------------------
    exec::PatchLoopOptions lo;
    lo.repo_root = repo_root;
    lo.max_iterations = adapter ? std::max(1, cli.max_iterations) : 1;

    exec::TestRunFn run_tests_fn;
    if (adapter) {
        run_tests_fn = [a = *adapter](const fs::path& root) { return exec::run_tests(a, root); };
    } else {
        // Stub runner that always passes — no real tests, so the
        // loop applies the first patch and returns PassedFirstTry.
        run_tests_fn = [](const fs::path&) {
            exec::TestReport tr;
            tr.passed = true;
            return tr;
        };
    }

    exec::PatchLoopHooks hooks;
    hooks.on_token = [](std::string_view delta) { std::cout << delta << std::flush; };
    hooks.on_proposed_patch = [&](const exec::Patch& patch) {
        std::cout << "\n--- proposed patch ---\n";
        render_diff(std::cout, patch, DiffRenderOptions{use_color});
        std::cout << "----------------------\n";
        if (cli.auto_approve) {
            std::cout << "auto-approving (--yes).\n";
            return true;
        }
        const auto d = prompt_decision(std::cin, std::cout);
        return d.has_value() && *d == ApprovalDecision::Approve;
    };
    hooks.on_test_result = [](int iter, const exec::TestReport& tr) {
        if (tr.passed) {
            fmt::print("iteration {}: tests passed.\n", iter);
        } else {
            fmt::print("iteration {}: tests failed ({} failure{}).\n",
                       iter,
                       tr.failures.size(),
                       tr.failures.size() == 1 ? "" : "s");
        }
    };

    exec::PatchLoopReport report;
    try {
        report = exec::run_patch_loop(*backend, run_tests_fn, lo, cli.task, "", hooks);
    } catch (const std::exception& e) {
        fmt::print(stderr, "\nerror: patch loop failed: {}\n", e.what());
        return 1;
    }

    fmt::print("\nresult: {} ({} iteration{}).\n",
               outcome_label(report.outcome),
               report.iterations,
               report.iterations == 1 ? "" : "s");

    switch (report.outcome) {
        case exec::PatchLoopOutcome::PassedFirstTry:
        case exec::PatchLoopOutcome::PassedAfterRetry:
            return 0;
        case exec::PatchLoopOutcome::GaveUp:
        case exec::PatchLoopOutcome::Aborted:
        case exec::PatchLoopOutcome::NoPatch:
            return 1;
    }
    return 1;
}

}  // namespace vectra::cli
