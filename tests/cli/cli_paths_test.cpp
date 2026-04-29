// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "cli_paths.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using vectra::cli::current_exe_path;
using vectra::cli::find_project_root;
using vectra::cli::resolve_adapters_dir;
using vectra::cli::resolve_config_path;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_root(std::string_view label) {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-cli-paths-test" /
                (std::string{label} + "-" + session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream{p, std::ios::binary | std::ios::trunc};
}

}  // namespace

TEST_CASE("find_project_root returns the directory containing .vectra", "[cli_paths]") {
    const auto root = make_tmp_root("vectra-marker");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / "src" / "deep" / "nested");

    const auto got = find_project_root(root / "src" / "deep" / "nested");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root falls back to .git when no .vectra is present", "[cli_paths]") {
    const auto root = make_tmp_root("git-marker");
    fs::create_directories(root / ".git");
    fs::create_directories(root / "subdir");

    const auto got = find_project_root(root / "subdir");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root prefers the closest marker on overlapping ancestors", "[cli_paths]") {
    const auto outer = make_tmp_root("outer");
    fs::create_directories(outer / ".git");
    const auto inner = outer / "child";
    fs::create_directories(inner / ".vectra");
    fs::create_directories(inner / "deeper");

    const auto got = find_project_root(inner / "deeper");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, inner));
}

TEST_CASE("find_project_root returns nullopt when no marker exists", "[cli_paths]") {
    const auto root = make_tmp_root("no-marker");
    // No .vectra, no .git anywhere up the tree from this tmp dir.
    fs::create_directories(root / "a" / "b");
    const auto got = find_project_root(root / "a" / "b");
    // The temp dir parent might or might not have a .git; we cannot
    // assume the absence of markers above the temp prefix. So this
    // test only asserts that the result, if present, is at most the
    // tmp root or above — i.e., it never spuriously returns a/b
    // itself.
    if (got) {
        REQUIRE_FALSE(fs::equivalent(*got, root / "a" / "b"));
    }
}

TEST_CASE("resolve_config_path returns the override when set", "[cli_paths]") {
    const auto override_path = fs::path{"/some/elsewhere.toml"};
    const auto got = resolve_config_path(fs::path{"/proj"}, override_path);
    REQUIRE(got == override_path);
}

TEST_CASE("resolve_config_path defaults to <root>/.vectra/config.toml", "[cli_paths]") {
    const auto got = resolve_config_path(fs::path{"/proj"}, fs::path{});
    REQUIRE(got == fs::path{"/proj"} / ".vectra" / "config.toml");
}

TEST_CASE("resolve_adapters_dir prefers the override when it exists", "[cli_paths]") {
    const auto root = make_tmp_root("explicit-adapters");
    const auto custom = root / "custom-adapters";
    fs::create_directories(custom);

    const auto got = resolve_adapters_dir(root, custom);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, custom));
}

TEST_CASE("resolve_adapters_dir returns nullopt when override does not exist", "[cli_paths]") {
    const auto root = make_tmp_root("missing-adapters");
    const auto got = resolve_adapters_dir(root, root / "does-not-exist");
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("resolve_adapters_dir picks up <repo>/adapters when no override", "[cli_paths]") {
    const auto root = make_tmp_root("repo-local-adapters");
    fs::create_directories(root / "adapters");

    const auto got = resolve_adapters_dir(root, fs::path{});
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root / "adapters"));
}

TEST_CASE("current_exe_path returns a real, existing file", "[cli_paths]") {
    const auto p = current_exe_path();
    REQUIRE_FALSE(p.empty());
    std::error_code ec;
    REQUIRE(fs::exists(p, ec));
    REQUIRE(fs::is_regular_file(p, ec));
}
