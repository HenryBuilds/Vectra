// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/patch_loop.hpp"

#include <fmt/format.h>

#include <exception>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vectra/exec/apply.hpp"

namespace vectra::exec {

namespace {

constexpr std::string_view kDefaultSystemPrompt =
    "You are Vectra, a code-editing assistant. When asked to modify code, "
    "respond with a single unified diff (--- a/path / +++ b/path / @@ ... @@). "
    "Use repository-relative paths. If your previous patch failed tests, "
    "produce a corrected diff against the original code, not a delta on top.";

constexpr std::size_t kMaxFailuresQuoted = 5;
constexpr std::size_t kMaxFailureMessageSize = 1024;

[[nodiscard]] std::string format_failures(const TestReport& tr) {
    std::string out;
    const auto n = std::min(tr.failures.size(), kMaxFailuresQuoted);
    for (std::size_t i = 0; i < n; ++i) {
        out += "  - ";
        const auto& msg = tr.failures[i].message;
        if (msg.size() > kMaxFailureMessageSize) {
            out.append(msg, 0, kMaxFailureMessageSize);
            out += "...";
        } else {
            out += msg;
        }
        if (out.empty() || out.back() != '\n') {
            out += '\n';
        }
    }
    if (tr.failures.size() > n) {
        out += fmt::format("  (... {} more failures omitted)\n", tr.failures.size() - n);
    }
    return out;
}

}  // namespace

PatchLoopReport run_patch_loop(agent::LlmBackend& backend,
                               const TestRunFn& run_tests_fn,
                               const PatchLoopOptions& opts,
                               std::string_view user_request,
                               std::string_view system_prompt,
                               const PatchLoopHooks& hooks) {
    if (opts.repo_root.empty()) {
        throw std::runtime_error("run_patch_loop: repo_root is required");
    }
    if (!run_tests_fn) {
        throw std::runtime_error("run_patch_loop: run_tests_fn is required");
    }
    if (opts.max_iterations < 1) {
        throw std::runtime_error("run_patch_loop: max_iterations must be >= 1");
    }

    const std::string_view sys_prompt =
        system_prompt.empty() ? kDefaultSystemPrompt : system_prompt;

    std::vector<agent::ChatMessage> conv;
    conv.push_back({agent::ChatMessage::Role::System, std::string{sys_prompt}});
    conv.push_back({agent::ChatMessage::Role::User, std::string{user_request}});

    PatchLoopReport rep;

    for (int iter = 1; iter <= opts.max_iterations; ++iter) {
        rep.iterations = iter;

        agent::GenerateOptions go;
        go.on_token = hooks.on_token;
        std::string reply = backend.generate(conv, go);
        rep.model_replies.push_back(reply);
        conv.push_back({agent::ChatMessage::Role::Assistant, reply});

        Patch patch;
        try {
            patch = parse_unified_diff(reply);
        } catch (const std::exception& e) {
            // Malformed diff (e.g., truncated hunk). Feed the parse
            // error back and retry without counting an apply attempt.
            conv.push_back({agent::ChatMessage::Role::User,
                            fmt::format("Your reply did not contain a parseable unified "
                                        "diff: {}. Please respond with a corrected diff.",
                                        e.what())});
            continue;
        }

        if (patch.empty()) {
            // No diff at all. The model is not attempting a change;
            // self-healing has nothing to retry on.
            rep.outcome = PatchLoopOutcome::NoPatch;
            return rep;
        }

        if (hooks.on_proposed_patch && !hooks.on_proposed_patch(patch)) {
            rep.outcome = PatchLoopOutcome::Aborted;
            return rep;
        }

        ApplyOptions ao;
        ao.repo_root = opts.repo_root;
        ApplyResult ar;
        try {
            ar = apply_patch(patch, ao);
        } catch (const std::exception& e) {
            // Context mismatch or filesystem error. Tell the model
            // and retry. Validate-phase errors leave the tree clean
            // by contract (no writes happened).
            conv.push_back({agent::ChatMessage::Role::User,
                            fmt::format("The previous patch could not be applied: {}. "
                                        "Please produce a corrected unified diff.",
                                        e.what())});
            continue;
        }

        const TestReport tr = run_tests_fn(opts.repo_root);
        if (hooks.on_test_result) {
            hooks.on_test_result(iter, tr);
        }
        rep.last_test_report = tr;

        if (tr.passed) {
            rep.outcome =
                (iter == 1) ? PatchLoopOutcome::PassedFirstTry : PatchLoopOutcome::PassedAfterRetry;
            rep.final_backup_dir = ar.backup_dir;
            return rep;
        }

        // Tests failed. Rollback so the next iteration starts from
        // the original tree, then ask the model for a corrected diff.
        try {
            rollback(ar.backup_dir);
        } catch (...) {
            // Best-effort. We continue the conversation regardless;
            // the next apply will refuse on context mismatch and the
            // model will be told.
        }

        conv.push_back({agent::ChatMessage::Role::User,
                        fmt::format("Tests failed after applying your patch:\n{}\n"
                                    "Please produce a corrected unified diff against "
                                    "the original code (not a delta on top).",
                                    format_failures(tr))});
    }

    rep.outcome = PatchLoopOutcome::GaveUp;
    return rep;
}

}  // namespace vectra::exec
