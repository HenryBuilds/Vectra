// Copyright 2026 Vectra Contributors. Apache-2.0.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "vectra/agent/llm_backend.hpp"

using vectra::agent::AgentConfig;
using vectra::agent::open_backend;

namespace {

// Create a unique tmp config path per test invocation.
std::filesystem::path tmp_toml_path() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto base = std::filesystem::temp_directory_path() / "vectra-agent-test";
    std::filesystem::create_directories(base);
    return base / ("cfg-" + session + "-" + std::to_string(counter.fetch_add(1)) + ".toml");
}

void write_file(const std::filesystem::path& p, std::string_view contents) {
    std::ofstream out(p, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

}  // namespace

TEST_CASE("AgentConfig defaults match the documented Anthropic setup", "[agent]") {
    const AgentConfig cfg = AgentConfig::with_defaults();
    REQUIRE(cfg.backend == "anthropic");
    REQUIRE(cfg.api_key_env == "ANTHROPIC_API_KEY");
    REQUIRE(cfg.max_tokens > 0);
    REQUIRE(cfg.temperature == 0.0);
    // Pin the model default so a future diff that silently changes
    // it has to update this assertion.
    REQUIRE(cfg.model.starts_with("claude-"));
}

TEST_CASE("from_toml reads an [llm] table", "[agent]") {
    const auto path = tmp_toml_path();
    write_file(path, R"toml(
        [llm]
        backend         = "anthropic"
        model           = "claude-sonnet-4-6"
        api_key_env     = "MY_CUSTOM_KEY"
        max_tokens      = 2048
        temperature     = 0.2
        timeout_seconds = 60
    )toml");

    const auto cfg = AgentConfig::from_toml(path);
    REQUIRE(cfg.backend == "anthropic");
    REQUIRE(cfg.model == "claude-sonnet-4-6");
    REQUIRE(cfg.api_key_env == "MY_CUSTOM_KEY");
    REQUIRE(cfg.max_tokens == 2048);
    REQUIRE(cfg.temperature == 0.2);
    REQUIRE(cfg.timeout_seconds == 60);
}

TEST_CASE("from_toml falls back to defaults when [llm] is missing", "[agent]") {
    const auto path = tmp_toml_path();
    write_file(path, "# empty config\n");

    const auto cfg = AgentConfig::from_toml(path);
    REQUIRE(cfg.backend == "anthropic");  // default kept
    REQUIRE(cfg.api_key_env == "ANTHROPIC_API_KEY");
}

TEST_CASE("from_toml rejects empty backend / model / api_key_env", "[agent]") {
    const auto path = tmp_toml_path();
    write_file(path, R"toml(
        [llm]
        backend = ""
    )toml");
    REQUIRE_THROWS_AS(AgentConfig::from_toml(path), std::runtime_error);
}

TEST_CASE("from_toml surfaces TOML parse errors", "[agent]") {
    const auto path = tmp_toml_path();
    write_file(path, "this = is = not = valid = toml\n");
    REQUIRE_THROWS_AS(AgentConfig::from_toml(path), std::runtime_error);
}

TEST_CASE("open_backend errors when the API key env var is unset", "[agent]") {
    AgentConfig cfg = AgentConfig::with_defaults();
    cfg.api_key_env = "VECTRA_TEST_DEFINITELY_UNSET_KEY_NAME_42";

    // Defensive: make sure the env var is genuinely unset before the
    // assertion. We do not unset because tests may run in parallel
    // and POSIX unsetenv mutates global state.
    REQUIRE(std::getenv(cfg.api_key_env.c_str()) == nullptr);

    REQUIRE_THROWS_AS(open_backend(cfg), std::runtime_error);
}

TEST_CASE("open_backend errors on an unknown backend", "[agent]") {
    AgentConfig cfg = AgentConfig::with_defaults();
    cfg.backend = "totally-fictional";
    cfg.api_key_env = "PATH";  // any guaranteed-set env var works for this test

    REQUIRE_THROWS_AS(open_backend(cfg), std::runtime_error);
}
