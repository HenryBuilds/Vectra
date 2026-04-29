// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// LLM backend abstraction. Vectra's exec loop talks to whichever
// model the user has configured via this single interface; nothing
// downstream of the abstraction knows whether the bytes come from
// Anthropic's API, an OpenAI-compatible endpoint, a local llama.cpp
// run, or eventually a Claude Code subprocess.
//
// The interface is intentionally small: chat-style messages in,
// generated text out. Streaming, tool use, and structured-output
// modes are deferred to v2 of the abstraction; they only matter
// once we have multiple backends that all support them in similar
// shapes.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vectra::agent {

struct ChatMessage {
    enum class Role : std::uint8_t {
        System,     // background instructions for the assistant
        User,       // human input or retrieved context the assistant should read
        Assistant,  // prior assistant turns (for multi-turn conversations)
    };

    Role role;
    std::string content;
};

struct GenerateOptions {
    // Maximum tokens the model is allowed to emit.
    int max_tokens = 4096;

    // Sampling temperature. 0.0 is the right default for code
    // generation: deterministic, no creative drift.
    double temperature = 0.0;

    // Hard wall-clock cap on the request, in seconds. Backends that
    // talk over HTTPS use this as the read timeout; local backends
    // honour it best-effort.
    int timeout_seconds = 300;

    // Optional stop sequences. The backend appends them to whatever
    // model-specific stop tokens the backend already uses.
    std::vector<std::string> stop;

    // Optional streaming token callback. When set, the backend
    // requests an SSE / chunked response from the underlying API
    // and invokes the callback for each text delta as it arrives.
    // The full concatenated text is still returned by generate();
    // the callback is purely for incremental UI feedback (think
    // "let the user see the model typing").
    //
    // Threading: the callback is invoked synchronously from the
    // HTTP receive thread. Keep it cheap (printf, ring-buffer
    // append, ...) and never throw — exceptions across the
    // callback boundary corrupt the request lifecycle.
    std::function<void(std::string_view delta)> on_token;
};

// Abstract interface over a chat-style LLM. Concrete backends live
// under src/agent/<name>_backend.cpp.
class LlmBackend {
public:
    virtual ~LlmBackend() = default;

    // Run one round of generation against `messages`. Returns the
    // assistant's reply as a single string. Throws std::runtime_error
    // on transport failure, auth error, or any non-2xx status from a
    // remote backend.
    [[nodiscard]] virtual std::string generate(std::span<const ChatMessage> messages,
                                               const GenerateOptions& opts) = 0;

    // Identifier for logs and the `--backend` flag of the CLI.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

// Configuration loaded from .vectra/config.toml. The defaults match
// the recommended setup (Anthropic API, claude-sonnet-4-6, API key
// from ANTHROPIC_API_KEY) so a brand-new user with that env var set
// has a working tool with zero config work.
struct AgentConfig {
    // Backend identifier. Currently supported: "anthropic". Future:
    // "openai-compatible", "claude-code", "llama-cpp".
    std::string backend = "anthropic";

    // Model name as recognised by the chosen backend.
    std::string model = "claude-sonnet-4-6";

    // Environment variable that holds the API key. We never read or
    // write keys on disk — the config only references the env name.
    std::string api_key_env = "ANTHROPIC_API_KEY";

    // Optional endpoint override. Used by the openai-compatible
    // backend to point at OpenRouter / a local LM Studio / etc.
    std::string endpoint;

    int max_tokens = 4096;
    double temperature = 0.0;
    int timeout_seconds = 300;

    // Load from a TOML file. Throws std::runtime_error on parse
    // errors, unknown backend, or missing required fields. Missing
    // fields fall back to the defaults above.
    [[nodiscard]] static AgentConfig from_toml(const std::filesystem::path& path);

    // Defaults-only config: useful when the user has no
    // .vectra/config.toml and just wants to run with ANTHROPIC_API_KEY
    // exported.
    [[nodiscard]] static AgentConfig with_defaults() noexcept { return {}; }
};

// Factory: instantiate the right backend for the given config.
// Reads the API key from the configured environment variable and
// throws if it is missing or empty. The returned backend owns its
// own HTTP client / model handle and is safe to share across
// threads (concurrent generate() calls are serialised internally).
[[nodiscard]] std::unique_ptr<LlmBackend> open_backend(const AgentConfig& cfg);

}  // namespace vectra::agent
