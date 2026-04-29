// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Streaming Server-Sent-Events parser.
//
// SSE responses are delivered as a sequence of events terminated
// by blank lines. Each event has optional fields like `event:` and
// one or more `data:` lines. The wire format does not align cleanly
// to TCP segment boundaries — a single chunk delivered to our
// content-receiver may end in the middle of a line, the middle of
// an event, or right after a complete event.
//
// SseParser carries the "in-progress" state across feed() calls so
// the AnthropicBackend's content-receiver can stay simple: feed
// raw bytes, get back complete events when they become available.
//
// Internal to vectra-agent.

#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace vectra::agent::detail {

// One SSE event. `type` is empty when the server did not emit an
// `event:` field (the spec's default-event behaviour). `data`
// concatenates multiple `data:` lines with single newline
// separators, matching the spec.
struct SseEvent {
    std::string type;
    std::string data;
};

class SseParser {
public:
    // Append `bytes` to the rolling buffer and return any complete
    // events that can be extracted. Trailing partial events stay
    // buffered for the next feed() call.
    [[nodiscard]] std::vector<SseEvent> feed(std::string_view bytes);

private:
    std::string buffer_;
    SseEvent current_;
    bool has_current_ = false;
};

}  // namespace vectra::agent::detail
