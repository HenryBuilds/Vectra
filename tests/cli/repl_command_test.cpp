// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "repl_command.hpp"

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
using vectra::cli::ReplOptions;
using vectra::cli::run_repl;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_repo() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-repl-test" /
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

}  // namespace

TEST_CASE("run_repl exits cleanly on EOF before any prompt", "[repl]") {
    FakeBackend backend;
    std::istringstream in;
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(out.str().find("vectra REPL") != std::string::npos);
    REQUIRE(backend.cursor == 0);
}

TEST_CASE("run_repl quits on /quit without invoking the backend", "[repl]") {
    FakeBackend backend;
    std::istringstream in("/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.cursor == 0);
}

TEST_CASE("run_repl prints help on /help and continues until EOF", "[repl]") {
    FakeBackend backend;
    std::istringstream in("/help\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(out.str().find("Commands:") != std::string::npos);
}

TEST_CASE("run_repl streams a prose reply with no diff and stays in the loop", "[repl]") {
    FakeBackend backend;
    backend.replies = {"Just a chat response, no edits.\n"};

    std::istringstream in("hello\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.cursor == 1);
    REQUIRE(out.str().find("Just a chat response") != std::string::npos);
    // No proposed-patch banner when the reply has no diff.
    REQUIRE(out.str().find("--- proposed patch ---") == std::string::npos);
}

TEST_CASE("run_repl renders a diff and discards it on Reject", "[repl]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {
        "I'll change that for you:\n"
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-hello\n"
        "+goodbye\n"};

    std::istringstream in("change foo\nn\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.repo_root = repo;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(out.str().find("--- proposed patch ---") != std::string::npos);
    REQUIRE(out.str().find("patch discarded") != std::string::npos);
    // File untouched on reject.
    REQUIRE(read_file(repo / "foo.txt") == "hello\n");
}

TEST_CASE("run_repl applies a diff after Approve", "[repl]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-hello\n"
        "+goodbye\n"};

    std::istringstream in("change foo\ny\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.repo_root = repo;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(out.str().find("applied:") != std::string::npos);
    REQUIRE(read_file(repo / "foo.txt") == "goodbye\n");
}

TEST_CASE("run_repl exits when the user picks Quit at the approval prompt", "[repl]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "foo.txt", "hello\n");

    FakeBackend backend;
    backend.replies = {
        "--- a/foo.txt\n"
        "+++ b/foo.txt\n"
        "@@ -1,1 +1,1 @@\n"
        "-hello\n"
        "+goodbye\n"};

    std::istringstream in("change foo\nq\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.repo_root = repo;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    // File untouched: the user quit before applying.
    REQUIRE(read_file(repo / "foo.txt") == "hello\n");
}

TEST_CASE("run_repl forwards the system prompt and user turn to the backend", "[repl]") {
    FakeBackend backend;
    backend.replies = {"ok"};

    std::istringstream in("rename Foo to Bar\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.seen.size() == 1);
    const auto& msgs = backend.seen[0];
    REQUIRE(msgs.size() == 2);
    REQUIRE(msgs[0].role == ChatMessage::Role::System);
    REQUIRE(msgs[0].content.find("Vectra") != std::string::npos);
    REQUIRE(msgs[1].role == ChatMessage::Role::User);
    REQUIRE(msgs[1].content == "rename Foo to Bar");
}

TEST_CASE("run_repl carries conversation history across turns", "[repl]") {
    FakeBackend backend;
    backend.replies = {"first reply", "second reply"};

    std::istringstream in("first turn\nsecond turn\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.seen.size() == 2);

    // Turn 1: system + user.
    const auto& t1 = backend.seen[0];
    REQUIRE(t1.size() == 2);
    REQUIRE(t1[1].content == "first turn");

    // Turn 2: system + user1 + assistant1 + user2.
    const auto& t2 = backend.seen[1];
    REQUIRE(t2.size() == 4);
    REQUIRE(t2[0].role == ChatMessage::Role::System);
    REQUIRE(t2[1].role == ChatMessage::Role::User);
    REQUIRE(t2[1].content == "first turn");
    REQUIRE(t2[2].role == ChatMessage::Role::Assistant);
    REQUIRE(t2[2].content == "first reply");
    REQUIRE(t2[3].role == ChatMessage::Role::User);
    REQUIRE(t2[3].content == "second turn");
}

TEST_CASE("run_repl /clear drops the history but keeps the system prompt", "[repl]") {
    FakeBackend backend;
    backend.replies = {"first reply", "post-clear reply"};

    std::istringstream in("first turn\n/clear\nfresh turn\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.seen.size() == 2);

    // After /clear the second turn should look just like a fresh turn 1.
    const auto& t2 = backend.seen[1];
    REQUIRE(t2.size() == 2);
    REQUIRE(t2[0].role == ChatMessage::Role::System);
    REQUIRE(t2[1].role == ChatMessage::Role::User);
    REQUIRE(t2[1].content == "fresh turn");

    REQUIRE(out.str().find("history cleared") != std::string::npos);
}

TEST_CASE("run_repl /history reports the turn count", "[repl]") {
    FakeBackend backend;
    backend.replies = {"r1", "r2"};

    std::istringstream in("turn one\nturn two\n/history\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(out.str().find("history: 2 turns") != std::string::npos);
}

TEST_CASE("run_repl history_limit prunes the oldest turn pair", "[repl]") {
    FakeBackend backend;
    backend.replies = {"r1", "r2", "r3"};

    std::istringstream in("turn one\nturn two\nturn three\n/quit\n");
    std::ostringstream out;
    ReplOptions opts;
    opts.use_color = false;
    opts.history_limit = 1;  // keep only the most recent user/assistant pair

    REQUIRE(run_repl(in, out, backend, opts) == 0);
    REQUIRE(backend.seen.size() == 3);

    // Turn 3's view: system + (user2, assistant2) + user3 — the
    // "turn one" pair was pruned after turn 2 closed.
    const auto& t3 = backend.seen[2];
    REQUIRE(t3.size() == 4);
    REQUIRE(t3[0].role == ChatMessage::Role::System);
    REQUIRE(t3[1].role == ChatMessage::Role::User);
    REQUIRE(t3[1].content == "turn two");
    REQUIRE(t3[2].role == ChatMessage::Role::Assistant);
    REQUIRE(t3[2].content == "r2");
    REQUIRE(t3[3].role == ChatMessage::Role::User);
    REQUIRE(t3[3].content == "turn three");
}
