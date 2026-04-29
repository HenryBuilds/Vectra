// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "project_config.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using vectra::cli::load_project_config;
using vectra::cli::ProjectConfig;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_repo() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-config-test" /
                (session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

void write_config(const fs::path& repo_root, std::string_view contents) {
    fs::create_directories(repo_root / ".vectra");
    std::ofstream out(repo_root / ".vectra" / "config.toml", std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

}  // namespace

TEST_CASE("load_project_config returns an empty config when the file is absent",
          "[project_config]") {
    const auto repo = make_tmp_repo();
    const auto cfg = load_project_config(repo);
    REQUIRE(cfg.model.empty());
    REQUIRE(cfg.reranker.empty());
    REQUIRE(cfg.top_k == 0);
    REQUIRE(cfg.claude_binary.empty());
    REQUIRE(cfg.claude_extra_args.empty());
}

TEST_CASE("load_project_config reads [retrieve] and [claude] sections", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[retrieve]
model    = "qwen3-embed-8b"
reranker = "qwen3-rerank-0.6b"
top_k    = 12

[claude]
binary     = "claude"
extra_args = ["--model", "claude-opus-4-5"]
)toml");

    const auto cfg = load_project_config(repo);
    REQUIRE(cfg.model == "qwen3-embed-8b");
    REQUIRE(cfg.reranker == "qwen3-rerank-0.6b");
    REQUIRE(cfg.top_k == 12);
    REQUIRE(cfg.claude_binary == "claude");
    REQUIRE(cfg.claude_extra_args == std::vector<std::string>{"--model", "claude-opus-4-5"});
}

TEST_CASE("load_project_config tolerates missing sections / keys", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[retrieve]
model = "qwen3-embed-0.6b"
)toml");

    const auto cfg = load_project_config(repo);
    REQUIRE(cfg.model == "qwen3-embed-0.6b");
    REQUIRE(cfg.reranker.empty());
    REQUIRE(cfg.top_k == 0);
    REQUIRE(cfg.claude_binary.empty());
    REQUIRE(cfg.claude_extra_args.empty());
}

TEST_CASE("load_project_config rejects a non-string model", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[retrieve]
model = 42
)toml");
    REQUIRE_THROWS_AS(load_project_config(repo), std::runtime_error);
}

TEST_CASE("load_project_config rejects a negative top_k", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[retrieve]
top_k = -1
)toml");
    REQUIRE_THROWS_AS(load_project_config(repo), std::runtime_error);
}

TEST_CASE("load_project_config rejects a non-array extra_args", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[claude]
extra_args = "not an array"
)toml");
    REQUIRE_THROWS_AS(load_project_config(repo), std::runtime_error);
}

TEST_CASE("load_project_config rejects non-string entries inside extra_args", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, R"toml(
[claude]
extra_args = ["--model", 5]
)toml");
    REQUIRE_THROWS_AS(load_project_config(repo), std::runtime_error);
}

TEST_CASE("load_project_config surfaces TOML parse errors", "[project_config]") {
    const auto repo = make_tmp_repo();
    write_config(repo, "[retrieve\nmodel = \"x\"\n");  // unclosed table header
    REQUIRE_THROWS_AS(load_project_config(repo), std::runtime_error);
}
