// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/patch_loop.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using vectra::agent::ChatMessage;
using vectra::agent::GenerateOptions;
using vectra::agent::LlmBackend;
using vectra::exec::PatchLoopHooks;
using vectra::exec::PatchLoopOptions;
using vectra::exec::PatchLoopOutcome;
using vectra::exec::run_patch_loop;
using vectra::exec::TestFailure;
using vectra::exec::TestReport;
using vectra::exec::TestRunFn;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_repo() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-patchloop-test" /
                (session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& p, std::string_view contents) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

class FakeBackend final : public LlmBackend {
public:
    std::vector<std::string> replies;
    std::size_t cursor = 0;
    std::vector<std::vector<ChatMessage>> seen;

    [[nodiscard]] std::string generate(std::span<const ChatMessage> messages,
                                       const GenerateOptions& opts) override {
        seen.emplace_back(messages.begin(), messages.end());
        std::string r = cursor < replies.size() ? replies[cursor] : std::string{};
        ++cursor;
        if (opts.on_token) {
            opts.on_token(r);
        }
        return r;
    }
    [[nodiscard]] std::string_view name() const noexcept override { return "fake"; }
};

// Test runner that returns a scripted sequence of TestReports, one
// per call, then repeats the last entry forever.
class ScriptedRunner {
public:
    std::vector<TestReport> outcomes;
    int calls = 0;

    [[nodiscard]] TestRunFn fn() {
        return [this](const fs::path&) {
            const auto idx = std::min(static_cast<std::size_t>(calls), outcomes.size() - 1);
            ++calls;
            return outcomes[idx];
        };
    }
};

[[nodiscard]] TestReport pass() {
    TestReport r;
    r.passed = true;
    r.exit_code = 0;
    return r;
}

[[nodiscard]] TestReport fail(std::string msg) {
    TestReport r;
    r.passed = false;
    r.exit_code = 1;
    r.failures.push_back(TestFailure{std::move(msg)});
    return r;
}

constexpr std::string_view kHelloDiff =
    "--- a/foo.txt\n"
    "+++ b/foo.txt\n"
    "@@ -1,1 +1,1 @@\n"
    "-hello\n"
    "+goodbye\n";

}  // namespace

TEST_CASE("run_patch_loop returns NoPatch when the reply has no diff", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {"Sorry, I cannot help with that."};

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "do something");
    REQUIRE(report.outcome == PatchLoopOutcome::NoPatch);
    REQUIRE(report.iterations == 1);
    REQUIRE(runner.calls == 0);                         // tests never ran
    REQUIRE(read_file(repo / "foo.txt") == "hello\n");  // file untouched
}

TEST_CASE("run_patch_loop returns PassedFirstTry on a clean apply + green tests", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {std::string{kHelloDiff}};

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "rename greeting");
    REQUIRE(report.outcome == PatchLoopOutcome::PassedFirstTry);
    REQUIRE(report.iterations == 1);
    REQUIRE(runner.calls == 1);
    REQUIRE(read_file(repo / "foo.txt") == "goodbye\n");
}

TEST_CASE("run_patch_loop converges after a failed iteration", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    // First reply changes hello -> wrong; second reply changes hello -> goodbye.
    FakeBackend backend;
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-hello\n"
        "+wrong\n",
        std::string{kHelloDiff},
    };

    // First test run fails, second passes.
    ScriptedRunner runner;
    runner.outcomes = {fail("expected goodbye, got wrong"), pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "rename greeting");
    REQUIRE(report.outcome == PatchLoopOutcome::PassedAfterRetry);
    REQUIRE(report.iterations == 2);
    REQUIRE(runner.calls == 2);
    REQUIRE(read_file(repo / "foo.txt") == "goodbye\n");
}

TEST_CASE("run_patch_loop feeds the test failure into the retry conversation", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-hello\n"
        "+wrong\n",
        std::string{kHelloDiff},
    };

    ScriptedRunner runner;
    runner.outcomes = {fail("AssertionError: expected goodbye"), pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "rename greeting");
    REQUIRE(report.outcome == PatchLoopOutcome::PassedAfterRetry);
    REQUIRE(backend.seen.size() == 2);

    // The second turn must include the failure excerpt so the model
    // has something to fix.
    const auto& second_turn = backend.seen[1];
    bool saw_failure = false;
    for (const auto& m : second_turn) {
        if (m.role == ChatMessage::Role::User &&
            m.content.find("AssertionError: expected goodbye") != std::string::npos) {
            saw_failure = true;
            break;
        }
    }
    REQUIRE(saw_failure);
}

TEST_CASE("run_patch_loop gives up after max_iterations and rolls back the tree", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    // Always emit the same wrong patch.
    backend.replies = {
        std::string{kHelloDiff},
        std::string{kHelloDiff},
    };

    ScriptedRunner runner;
    runner.outcomes = {fail("still wrong"), fail("still wrong")};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 2;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "rename greeting");
    REQUIRE(report.outcome == PatchLoopOutcome::GaveUp);
    REQUIRE(report.iterations == 2);
    REQUIRE(runner.calls == 2);
    // After two failed iterations the loop rolled back; the file is
    // back to its original state.
    REQUIRE(read_file(repo / "foo.txt") == "hello\n");
}

TEST_CASE("run_patch_loop honours the approval hook", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {std::string{kHelloDiff}};

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    int hook_calls = 0;
    PatchLoopHooks hooks;
    hooks.on_proposed_patch = [&](const vectra::exec::Patch&) {
        ++hook_calls;
        return false;  // veto
    };

    const auto report = run_patch_loop(backend, runner.fn(), opts, "go", "", hooks);
    REQUIRE(report.outcome == PatchLoopOutcome::Aborted);
    REQUIRE(hook_calls == 1);
    REQUIRE(runner.calls == 0);
    REQUIRE(read_file(repo / "foo.txt") == "hello\n");
}

TEST_CASE("run_patch_loop retries on a malformed diff without counting an apply", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    // First reply has a truncated hunk body. parse_unified_diff
    // throws, the loop appends the parse error and retries.
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,5 +1,5 @@\n"
        " hello\n",
        std::string{kHelloDiff},
    };

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "fix");
    REQUIRE(report.outcome == PatchLoopOutcome::PassedAfterRetry);
    REQUIRE(report.iterations == 2);
    REQUIRE(runner.calls == 1);  // only the second iteration applied + tested
    REQUIRE(read_file(repo / "foo.txt") == "goodbye\n");
}

TEST_CASE("run_patch_loop retries when apply_patch rejects the diff", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    // First diff has wrong context lines; apply will refuse.
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-not-the-actual-line\n"
        "+something\n",
        std::string{kHelloDiff},
    };

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "fix");
    REQUIRE(report.outcome == PatchLoopOutcome::PassedAfterRetry);
    REQUIRE(report.iterations == 2);
    REQUIRE(runner.calls == 1);
    REQUIRE(read_file(repo / "foo.txt") == "goodbye\n");
}

TEST_CASE("run_patch_loop streams tokens through the on_token hook", "[patch_loop]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {std::string{kHelloDiff}};

    ScriptedRunner runner;
    runner.outcomes = {pass()};

    std::string captured;
    PatchLoopHooks hooks;
    hooks.on_token = [&](std::string_view delta) { captured.append(delta); };

    PatchLoopOptions opts;
    opts.repo_root = repo;
    opts.max_iterations = 3;

    const auto report = run_patch_loop(backend, runner.fn(), opts, "fix", "", hooks);
    REQUIRE(report.outcome == PatchLoopOutcome::PassedFirstTry);
    REQUIRE(captured.find("--- a/foo.txt") != std::string::npos);
}

TEST_CASE("run_patch_loop validates required options", "[patch_loop]") {
    FakeBackend backend;
    PatchLoopOptions opts;
    opts.max_iterations = 3;

    // empty repo_root
    REQUIRE_THROWS_AS(run_patch_loop(
                          backend, [](const fs::path&) { return pass(); }, opts, "x"),
                      std::runtime_error);

    opts.repo_root = make_tmp_repo();

    // null run_tests_fn
    REQUIRE_THROWS_AS(run_patch_loop(backend, TestRunFn{}, opts, "x"), std::runtime_error);

    // max_iterations < 1
    opts.max_iterations = 0;
    REQUIRE_THROWS_AS(run_patch_loop(
                          backend, [](const fs::path&) { return pass(); }, opts, "x"),
                      std::runtime_error);
}
