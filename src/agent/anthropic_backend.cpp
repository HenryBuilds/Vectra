// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "anthropic_backend.hpp"

#include <fmt/format.h>

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#include "sse_parser.hpp"

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
            return "system";  // hoisted out of msgs[]
    }
    return "user";
}

// Shared body construction. Streaming and batch differ only in the
// "stream" flag; the rest of the request shape is identical.
[[nodiscard]] nlohmann::json build_body(std::span<const ChatMessage> messages,
                                        const GenerateOptions& opts,
                                        std::string_view model,
                                        bool streaming) {
    nlohmann::json body;
    body["model"] = model;
    body["max_tokens"] = opts.max_tokens;
    body["temperature"] = opts.temperature;
    if (streaming) {
        body["stream"] = true;
    }

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
    return body;
}

[[nodiscard]] httplib::Headers build_headers(std::string_view api_key) {
    return {
        {"x-api-key", std::string(api_key)},
        {"anthropic-version", "2023-06-01"},
        {"content-type", "application/json"},
    };
}

void configure_client(httplib::SSLClient& cli, int timeout_seconds) {
    cli.set_follow_location(true);
    cli.enable_server_certificate_verification(true);
    cli.set_read_timeout(timeout_seconds, 0);
    cli.set_connection_timeout(30, 0);
}

// Extract a useful error message from an Anthropic JSON error body.
// Falls back to the raw body text when the body is not JSON or the
// expected fields are missing.
[[nodiscard]] std::string extract_error_detail(std::string_view raw) {
    try {
        const auto j = nlohmann::json::parse(raw);
        if (j.contains("error") && j["error"].contains("message")) {
            return j["error"]["message"].get<std::string>();
        }
    } catch (...) {
        // fall through
    }
    return std::string(raw);
}

}  // namespace

AnthropicBackend::AnthropicBackend(std::string model, std::string api_key) noexcept
    : model_(std::move(model)), api_key_(std::move(api_key)) {}

std::string AnthropicBackend::generate(std::span<const ChatMessage> messages,
                                       const GenerateOptions& opts) {
    const bool streaming = static_cast<bool>(opts.on_token);
    const auto body = build_body(messages, opts, model_, streaming);
    const std::string body_str = body.dump();
    const auto headers = build_headers(api_key_);

    httplib::SSLClient cli("api.anthropic.com");
    configure_client(cli, opts.timeout_seconds);

    if (!streaming) {
        // ---- Batch path -----------------------------------------------
        const auto res = cli.Post("/v1/messages", headers, body_str, "application/json");
        if (!res) {
            throw std::runtime_error(fmt::format("anthropic request failed (httplib error {})",
                                                 static_cast<int>(res.error())));
        }
        if (res->status < 200 || res->status >= 300) {
            throw std::runtime_error(
                fmt::format("anthropic HTTP {}: {}", res->status, extract_error_detail(res->body)));
        }

        nlohmann::json resp;
        try {
            resp = nlohmann::json::parse(res->body);
        } catch (const nlohmann::json::parse_error& e) {
            throw std::runtime_error(
                fmt::format("anthropic response is not valid JSON: {}", e.what()));
        }

        if (!resp.contains("content") || !resp["content"].is_array() || resp["content"].empty()) {
            throw std::runtime_error("anthropic response missing or empty 'content' array");
        }

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

    // ---- Streaming path ------------------------------------------------
    // Anthropic emits SSE for stream=true with 2xx but a regular JSON
    // error body for 4xx/5xx. cpp-httplib's Post overload here doesn't
    // expose the status until the call returns, so we always feed the
    // SseParser and also keep a raw copy of the body for the error
    // path. A 4xx JSON body has no "\n\n" event terminator, so the
    // parser produces no events — on_token won't fire spuriously.
    std::string raw_body;
    std::string accumulated;
    std::string stream_error;
    SseParser parser;

    auto content_receiver = [&](const char* data, std::size_t length) {
        raw_body.append(data, length);

        for (auto& event : parser.feed(std::string_view(data, length))) {
            if (event.type == "content_block_delta") {
                try {
                    const auto j = nlohmann::json::parse(event.data);
                    if (j.contains("delta") && j["delta"].is_object() &&
                        j["delta"].value("type", std::string{}) == "text_delta" &&
                        j["delta"].contains("text")) {
                        const auto text = j["delta"]["text"].get<std::string>();
                        accumulated += text;
                        if (opts.on_token) {
                            opts.on_token(text);
                        }
                    }
                } catch (...) {
                    // Malformed payload — drop it. The server's
                    // message_stop closes the stream regardless.
                }
            } else if (event.type == "error") {
                stream_error = extract_error_detail(event.data);
            }
        }
        return true;
    };

    const auto res =
        cli.Post("/v1/messages", headers, body_str, "application/json", content_receiver);

    if (!res) {
        throw std::runtime_error(
            fmt::format("anthropic streaming request failed (httplib error {})",
                        static_cast<int>(res.error())));
    }
    if (res->status < 200 || res->status >= 300) {
        throw std::runtime_error(
            fmt::format("anthropic HTTP {}: {}", res->status, extract_error_detail(raw_body)));
    }
    if (!stream_error.empty()) {
        throw std::runtime_error(fmt::format("anthropic streaming error: {}", stream_error));
    }
    if (accumulated.empty()) {
        throw std::runtime_error("anthropic streaming response had no text deltas");
    }
    return accumulated;
}

}  // namespace vectra::agent::detail
