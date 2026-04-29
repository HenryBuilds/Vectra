// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "sse_parser.hpp"

#include <utility>

namespace vectra::agent::detail {

std::vector<SseEvent> SseParser::feed(std::string_view bytes) {
    buffer_.append(bytes);

    std::vector<SseEvent> events;
    std::size_t cursor = 0;

    while (cursor < buffer_.size()) {
        // Scan for a complete line. If we cannot find one, the
        // remaining tail must wait for the next feed().
        const auto eol = buffer_.find('\n', cursor);
        if (eol == std::string::npos) {
            break;
        }

        std::string_view line(buffer_.data() + cursor, eol - cursor);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        cursor = eol + 1;

        // Empty line = event boundary. Dispatch the in-progress
        // event if there is one and reset.
        if (line.empty()) {
            if (has_current_) {
                events.push_back(std::move(current_));
                current_ = {};
                has_current_ = false;
            }
            continue;
        }

        // Per SSE spec: lines beginning with ':' are comments and
        // are silently ignored. EventSource clients use them as
        // keep-alives.
        if (line.front() == ':') {
            continue;
        }

        // Split on the first ':'. A line without a colon names a
        // field with empty value (per spec); we record the field
        // but the empty value is effectively a no-op for the only
        // fields we care about.
        const auto colon = line.find(':');
        std::string_view field;
        std::string_view value;
        if (colon == std::string_view::npos) {
            field = line;
        } else {
            field = line.substr(0, colon);
            value = line.substr(colon + 1);
            // Per spec: a single leading space after the colon is
            // stripped. Two-space prefixes preserve the second
            // space.
            if (!value.empty() && value.front() == ' ') {
                value.remove_prefix(1);
            }
        }

        if (field == "event") {
            current_.type = std::string(value);
            has_current_ = true;
        } else if (field == "data") {
            if (!current_.data.empty()) {
                current_.data.push_back('\n');
            }
            current_.data.append(value);
            has_current_ = true;
        }
        // Other fields ("id", "retry", or unknown) are accepted but
        // not surfaced — Vectra has no use for them today.
    }

    // Drop the consumed prefix from the buffer so the next feed()
    // does not re-scan it.
    buffer_.erase(0, cursor);
    return events;
}

}  // namespace vectra::agent::detail
