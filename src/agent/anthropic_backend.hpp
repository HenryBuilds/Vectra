// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Direct HTTPS client against Anthropic's Messages API. Internal to
// vectra-agent.

#pragma once

#include <string>

#include "vectra/agent/llm_backend.hpp"

namespace vectra::agent::detail {

class AnthropicBackend : public LlmBackend {
public:
    AnthropicBackend(std::string model, std::string api_key) noexcept;

    [[nodiscard]] std::string generate(std::span<const ChatMessage> messages,
                                       const GenerateOptions& opts) override;

    [[nodiscard]] std::string_view name() const noexcept override { return "anthropic"; }

private:
    std::string model_;
    std::string api_key_;
};

}  // namespace vectra::agent::detail
