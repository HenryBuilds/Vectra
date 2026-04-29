// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Self-healing patch loop. Each iteration:
//   1. Ask the LLM for a unified diff.
//   2. Parse the reply. Empty diff -> end (NoPatch).
//   3. Run the caller's approval gate. False -> end (Aborted).
//   4. Apply the patch with backup.
//   5. Run the project's tests via the injected TestRunFn.
//   6. Tests pass -> done. Tests fail -> rollback, append the
//      failure excerpt to the conversation, and retry.
//
// The loop bounds itself by max_iterations. After that many failed
// attempts the working tree is left in its original state (the
// rollback at the end of each failed iteration guarantees this) and
// the report comes back as GaveUp.
//
// The test runner is a callback rather than a TestAdapter because
// (a) the loop's correctness is independent of how the tests are
// driven, and (b) unit tests can pass a deterministic stub that
// never spawns a subprocess.

#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/agent/llm_backend.hpp"
#include "vectra/exec/diff.hpp"
#include "vectra/exec/test_runner.hpp"

namespace vectra::exec {

enum class PatchLoopOutcome : std::uint8_t {
    PassedFirstTry,    // initial patch applied and tests passed
    PassedAfterRetry,  // converged after one or more fix iterations
    GaveUp,            // hit max_iterations without passing
    Aborted,           // approval hook returned false
    NoPatch,           // model produced no parseable diff
};

struct PatchLoopOptions {
    std::filesystem::path repo_root;
    int max_iterations = 3;
};

// Pluggable test driver. Production callers wrap exec::run_tests with
// a captured TestAdapter; tests inject their own.
using TestRunFn = std::function<TestReport(const std::filesystem::path&)>;

struct PatchLoopHooks {
    // Approval gate. Return true to apply, false to abort the loop.
    std::function<bool(const Patch&)> on_proposed_patch;

    // Per-iteration test outcome notification (1-indexed).
    std::function<void(int iteration, const TestReport&)> on_test_result;

    // Token-by-token streaming of the model's reply.
    std::function<void(std::string_view delta)> on_token;
};

struct PatchLoopReport {
    PatchLoopOutcome outcome = PatchLoopOutcome::NoPatch;
    int iterations = 0;
    std::filesystem::path final_backup_dir;
    TestReport last_test_report;

    // Raw model replies in order, one per iteration. Useful for
    // logging and post-hoc debugging.
    std::vector<std::string> model_replies;
};

// Drive the loop. Throws std::runtime_error when repo_root is empty,
// run_tests_fn is null, or max_iterations is < 1. Backend errors
// propagate. Apply / rollback errors are caught and folded into the
// retry conversation so a single bad iteration cannot kill the run.
[[nodiscard]] PatchLoopReport run_patch_loop(agent::LlmBackend& backend,
                                             const TestRunFn& run_tests_fn,
                                             const PatchLoopOptions& opts,
                                             std::string_view user_request,
                                             std::string_view system_prompt = {},
                                             const PatchLoopHooks& hooks = {});

}  // namespace vectra::exec
