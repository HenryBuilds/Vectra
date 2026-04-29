// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/test_runner.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using vectra::exec::build_report;
using vectra::exec::load_adapters;
using vectra::exec::select_adapter;
using vectra::exec::TestAdapter;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_dir(std::string_view label) {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-exec-test" /
                (std::string{label} + "-" + session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& p, std::string_view contents) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

}  // namespace

TEST_CASE("load_adapters parses every TOML manifest in the directory", "[test_runner]") {
    const auto dir = make_tmp_dir("adapters");
    write_file(dir / "alpha.toml", R"toml(
name          = "alpha"
description   = "alpha runner"
detect_files  = ["alpha.lock"]
priority      = 10
test_command  = ["alpha", "test"]
error_format  = "auto"
)toml");
    write_file(dir / "beta.toml", R"toml(
name          = "beta"
detect_files  = ["beta.cfg"]
priority      = 5
build_command = ["beta", "build"]
test_command  = ["beta", "test"]
error_format  = "regex"
error_pattern = "^FAIL: (.+)$"
)toml");
    // Non-toml file must be ignored.
    write_file(dir / "README.md", "not a manifest\n");

    const auto adapters = load_adapters(dir);
    REQUIRE(adapters.size() == 2);

    // load_adapters sorts by name, so [0]=alpha, [1]=beta.
    REQUIRE(adapters[0].name == "alpha");
    REQUIRE(adapters[0].priority == 10);
    REQUIRE(adapters[0].detect_files == std::vector<std::string>{"alpha.lock"});
    REQUIRE(adapters[0].test_command == std::vector<std::string>{"alpha", "test"});

    REQUIRE(adapters[1].name == "beta");
    REQUIRE(adapters[1].error_format == "regex");
    REQUIRE(adapters[1].error_pattern == "^FAIL: (.+)$");
    REQUIRE(adapters[1].build_command == std::vector<std::string>{"beta", "build"});
}

TEST_CASE("load_adapters throws on a missing directory", "[test_runner]") {
    const auto missing = make_tmp_dir("missing") / "does-not-exist";
    REQUIRE_THROWS_AS(load_adapters(missing), std::runtime_error);
}

TEST_CASE("load_adapters rejects a manifest missing the 'name' field", "[test_runner]") {
    const auto dir = make_tmp_dir("nameless");
    write_file(dir / "broken.toml", R"toml(
detect_files = ["foo"]
priority     = 1
)toml");
    REQUIRE_THROWS_AS(load_adapters(dir), std::runtime_error);
}

TEST_CASE("select_adapter returns nullopt when no detect_files match", "[test_runner]") {
    const auto root = make_tmp_dir("empty-root");
    TestAdapter a;
    a.name = "alpha";
    a.detect_files = {"Cargo.toml"};
    a.priority = 50;

    const std::vector<TestAdapter> adapters{a};
    const auto pick = select_adapter(adapters, root);
    REQUIRE_FALSE(pick.has_value());
}

TEST_CASE("select_adapter prefers higher priority on overlapping matches", "[test_runner]") {
    const auto root = make_tmp_dir("polyglot");
    write_file(root / "Cargo.toml", "");
    write_file(root / "package.json", "{}");

    TestAdapter cargo;
    cargo.name = "cargo";
    cargo.detect_files = {"Cargo.toml"};
    cargo.priority = 80;

    TestAdapter npm;
    npm.name = "npm";
    npm.detect_files = {"package.json"};
    npm.priority = 60;

    const std::vector<TestAdapter> adapters{npm, cargo};
    const auto pick = select_adapter(adapters, root);
    REQUIRE(pick.has_value());
    REQUIRE(pick->name == "cargo");
}

TEST_CASE("select_adapter accepts any of multiple detect_files", "[test_runner]") {
    const auto root = make_tmp_dir("pytest-root");
    write_file(root / "tox.ini", "[tox]\n");

    TestAdapter pytest;
    pytest.name = "pytest";
    pytest.detect_files = {"pyproject.toml", "pytest.ini", "setup.cfg", "tox.ini"};
    pytest.priority = 50;

    const std::vector<TestAdapter> adapters{pytest};
    const auto pick = select_adapter(adapters, root);
    REQUIRE(pick.has_value());
    REQUIRE(pick->name == "pytest");
}

TEST_CASE("build_report flags pass on exit code 0 with no failures", "[test_runner]") {
    TestAdapter a;
    a.name = "x";
    a.error_format = "auto";

    const auto rep = build_report(a, 0, "all good\n", std::chrono::milliseconds{42});
    REQUIRE(rep.passed);
    REQUIRE(rep.exit_code == 0);
    REQUIRE(rep.failures.empty());
    REQUIRE(rep.duration == std::chrono::milliseconds{42});
}

TEST_CASE("build_report extracts regex matches as separate failures", "[test_runner]") {
    TestAdapter a;
    a.name = "pytest-like";
    a.error_format = "regex";
    a.error_pattern = R"(^E\s+(.+)$)";

    const std::string output =
        "running tests\n"
        "E   AssertionError: expected 1 got 2\n"
        "more output\n"
        "E   ZeroDivisionError: division by zero\n"
        "summary: 2 failed\n";

    const auto rep = build_report(a, 1, output);
    REQUIRE_FALSE(rep.passed);
    REQUIRE(rep.failures.size() == 2);
    REQUIRE(rep.failures[0].message == "AssertionError: expected 1 got 2");
    REQUIRE(rep.failures[1].message == "ZeroDivisionError: division by zero");
}

TEST_CASE("build_report falls back to a tail excerpt when no regex matches", "[test_runner]") {
    TestAdapter a;
    a.name = "pytest-like";
    a.error_format = "regex";
    a.error_pattern = R"(^E\s+(.+)$)";

    const auto rep = build_report(a, 1, "no matching lines here\n");
    REQUIRE_FALSE(rep.passed);
    REQUIRE(rep.failures.size() == 1);
    REQUIRE(rep.failures[0].message == "no matching lines here\n");
}

TEST_CASE("build_report uses tail excerpt for non-regex error_format", "[test_runner]") {
    TestAdapter a;
    a.name = "compiler-like";
    a.error_format = "compiler";

    const auto rep = build_report(a, 2, "error: undefined reference to foo\n");
    REQUIRE_FALSE(rep.passed);
    REQUIRE(rep.failures.size() == 1);
    REQUIRE(rep.failures[0].message == "error: undefined reference to foo\n");
}

TEST_CASE("build_report tail excerpt snaps to a line boundary", "[test_runner]") {
    TestAdapter a;
    a.name = "x";
    a.error_format = "auto";

    // Build an output much larger than the 4 KiB tail budget so the
    // excerpt has to drop a prefix. The line boundary discipline
    // means the excerpt must not begin mid-line.
    std::string big;
    big.reserve(20 * 1024);
    for (int i = 0; i < 1000; ++i) {
        big += "line " + std::to_string(i) + " of the run\n";
    }

    const auto rep = build_report(a, 1, big);
    REQUIRE(rep.failures.size() == 1);
    const auto& msg = rep.failures[0].message;
    REQUIRE_FALSE(msg.empty());
    // First character must be the start of a line, not a continuation.
    REQUIRE(msg.starts_with("line "));
    // And the tail is bounded.
    REQUIRE(msg.size() <= 4096);
}

TEST_CASE("build_report falls back when error_pattern is invalid regex", "[test_runner]") {
    TestAdapter a;
    a.name = "broken-regex";
    a.error_format = "regex";
    a.error_pattern = "[unterminated";  // not a valid ECMAScript regex

    const auto rep = build_report(a, 1, "some output\n");
    REQUIRE_FALSE(rep.passed);
    REQUIRE(rep.failures.size() == 1);
    REQUIRE(rep.failures[0].message == "some output\n");
}
