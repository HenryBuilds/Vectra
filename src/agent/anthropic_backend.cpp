// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "anthropic_backend.hpp"

#include <fmt/format.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <httplib.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vectra::agent::detail {

namespace {

[[nodiscard]] std::string_view role_str(ChatMessage::Role role) noexcept {
    switch (role) {
        case ChatMessage::Role::User:
            return "user";
        case ChatMessage::Role::Assistant:
            return "assistant";
        case ChatMessage::Role::System:
            return "system";  // not used in Anthropic msgs[]
    }
    return "user";
}

}  // namespace

AnthropicBackend::AnthropicBackend(std::string model, std::string api_key) noexcept
    : model_(std::move(model)), api_key_(std::move(api_key)) {}

std::string AnthropicBackend::generate(std::span<const ChatMessage> messages,
                                       const GenerateOptions& opts) {
    // ---- Build request body ---------------------------------------------
    // Anthropic's Messages API splits "system" out of the messages array
    // into a top-level field. Concatenate any system messages we received
    // so the user can compose multiple system instructions if they want.
    nlohmann::json body;
    body["model"] = model_;
    body["max_tokens"] = opts.max_tokens;
    body["temperature"] = opts.temperature;

    std::string system_prompt;
    nlohmann::json msgs = nlohmann::json::array();
    for (const auto& m : messages) {
        if (m.role == ChatMessage::Role::System) {
            if (!system_prompt.empty())
                system_prompt += "\n\n";
            system_prompt += m.content;
            continue;
        }
        msgs.push_back({
            {"role", role_str(m.role)},
            {"content", m.content},
        });
    }
    body["messages"] = std::move(msgs);
    if (!system_prompt.empty()) {
        body["system"] = system_prompt;
    }
    if (!opts.stop.empty()) {
        body["stop_sequences"] = opts.stop;
    }

    // ---- Send the request -----------------------------------------------
    httplib::SSLClient cli("api.anthropic.com");
    cli.set_follow_location(true);
    cli.enable_server_certificate_verification(true);
    cli.set_read_timeout(opts.timeout_seconds, 0);
    cli.set_connection_timeout(30, 0);

    const httplib::Headers headers = {
        {"x-api-key", api_key_},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };

    const std::string body_str = body.dump();
    auto res = cli.Post("/v1/messages", headers, body_str, "application/json");

    if (!res) {
        throw std::runtime_error(fmt::format("anthropic request failed (httplib error {})",
                                             static_cast<int>(res.error())));
    }
    if (res->status < 200 || res->status >= 300) {
        // The API returns JSON error bodies; surface the message
        // directly so the user can act on it (rate limit, bad key,
        // model not found, etc.).
        std::string detail = res->body;
        try {
            const auto err = nlohmann::json::parse(res->body);
            if (err.contains("error") && err["error"].contains("message")) {
                detail = err["error"]["message"].get<std::string>();
            }
        } catch (...) {
            // Non-JSON error body — fall back to the raw response.
        }
        throw std::runtime_error(fmt::format("anthropic HTTP {}: {}", res->status, detail));
    }

    // ---- Parse the response ---------------------------------------------
    nlohmann::json resp;
    try {
        resp = nlohmann::json::parse(res->body);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error(fmt::format("anthropic response is not valid JSON: {}", e.what()));
    }

    if (!resp.contains("content") || !resp["content"].is_array() || resp["content"].empty()) {
        throw std::runtime_error("anthropic response missing or empty 'content' array");
    }

    // Concatenate all text-typed content blocks. The current API
    // emits at most one block when no tools are involved, but the
    // shape allows for several.
    std::string out;
    for (const auto& block : resp["content"]) {
        if (block.value("type", std::string{}) == "text" && block.contains("text")) {
            if (!out.empty())
                out += "\n";
            out += block["text"].get<std::string>();
        }
    }
    if (out.empty()) {
        throw std::runtime_error("anthropic response had content blocks but no text content");
    }
    return out;
}

}  // namespace vectra::agent::detail
