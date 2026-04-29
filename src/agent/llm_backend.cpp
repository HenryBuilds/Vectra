// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/agent/llm_backend.hpp"

#include <fmt/format.h>

#include <cstdlib>
#include <stdexcept>
#include <toml++/toml.hpp>

#include "anthropic_backend.hpp"

namespace vectra::agent {

namespace {

template <typename T>
[[nodiscard]] T get_or(const toml::table& tbl, std::string_view key, T fallback) {
    if (const auto* node = tbl.get(key); node != nullptr) {
        if (auto v = node->value<T>(); v.has_value())
            return *v;
    }
    return fallback;
}

}  // namespace

AgentConfig AgentConfig::from_toml(const std::filesystem::path& path) {
    toml::table doc;
    try {
        doc = toml::parse_file(path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            fmt::format("agent config parse error in {}: {}", path.string(), e.description()));
    }

    AgentConfig cfg;
    const auto* llm_node = doc.get("llm");
    if (llm_node == nullptr || !llm_node->is_table()) {
        // No [llm] section — defaults are fine. The user may have an
        // empty config to be filled in later.
        return cfg;
    }
    const auto& llm = *llm_node->as_table();

    cfg.backend = get_or<std::string>(llm, "backend", cfg.backend);
    cfg.model = get_or<std::string>(llm, "model", cfg.model);
    cfg.api_key_env = get_or<std::string>(llm, "api_key_env", cfg.api_key_env);
    cfg.endpoint = get_or<std::string>(llm, "endpoint", cfg.endpoint);
    cfg.max_tokens = get_or<int>(llm, "max_tokens", cfg.max_tokens);
    cfg.temperature = get_or<double>(llm, "temperature", cfg.temperature);
    cfg.timeout_seconds = get_or<int>(llm, "timeout_seconds", cfg.timeout_seconds);

    if (cfg.backend.empty()) {
        throw std::runtime_error("agent config: [llm].backend must not be empty");
    }
    if (cfg.model.empty()) {
        throw std::runtime_error("agent config: [llm].model must not be empty");
    }
    if (cfg.api_key_env.empty()) {
        throw std::runtime_error("agent config: [llm].api_key_env must not be empty");
    }
    return cfg;
}

std::unique_ptr<LlmBackend> open_backend(const AgentConfig& cfg) {
    // Resolve the API key from the configured environment variable.
    // We never accept the key as a CLI argument or config field —
    // doing so would invite users to leak it via shell history,
    // version control, or process listings.
    const char* api_key = std::getenv(cfg.api_key_env.c_str());
    if (api_key == nullptr || *api_key == '\0') {
        throw std::runtime_error(
            fmt::format("environment variable '{}' is not set or is empty. "
                        "Either export it or change [llm].api_key_env in your config.",
                        cfg.api_key_env));
    }

    if (cfg.backend == "anthropic") {
        return std::make_unique<detail::AnthropicBackend>(cfg.model, api_key);
    }

    // Other backends ("openai-compatible", "claude-code", "llama-cpp")
    // land in subsequent commits.
    throw std::runtime_error(
        fmt::format("agent config: backend '{}' is not implemented in this build. "
                    "Supported: anthropic.",
                    cfg.backend));
}

}  // namespace vectra::agent
