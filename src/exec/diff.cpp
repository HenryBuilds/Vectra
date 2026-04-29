// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/diff.hpp"

#include <fmt/format.h>

#include <charconv>
#include <stdexcept>
#include <string>

namespace vectra::exec {

namespace {

// Iterate `text` line by line without copying. Each call returns
// the next line excluding any terminator and advances `cursor`
// past it. Returns false when no more lines remain.
[[nodiscard]] bool next_line(std::string_view text,
                             std::size_t& cursor,
                             std::string_view& line_out) {
    if (cursor >= text.size()) {
        return false;
    }
    const auto end = text.find('\n', cursor);
    if (end == std::string_view::npos) {
        line_out = text.substr(cursor);
        cursor = text.size();
    } else {
        std::string_view raw = text.substr(cursor, end - cursor);
        // Strip a trailing CR so CRLF-terminated input round-trips
        // through our LF-only internal representation.
        if (!raw.empty() && raw.back() == '\r') {
            raw.remove_suffix(1);
        }
        line_out = raw;
        cursor = end + 1;
    }
    return true;
}

// Strip the optional "a/" / "b/" prefix that git-style diffs use.
[[nodiscard]] std::string strip_path_prefix(std::string_view path) {
    if (path == "/dev/null") {
        return std::string{path};
    }
    if (path.size() > 2 && (path[0] == 'a' || path[0] == 'b') && path[1] == '/') {
        return std::string{path.substr(2)};
    }
    return std::string{path};
}

// Read the path token after a "--- " or "+++ " marker. Stops at the
// first whitespace; the "yyyy-mm-dd hh:mm:ss timezone" timestamp
// some tools append after the path is silently dropped.
[[nodiscard]] std::string parse_path_token(std::string_view header_after_marker) {
    auto end = header_after_marker.find('\t');
    if (end == std::string_view::npos) {
        end = header_after_marker.size();
    }
    return strip_path_prefix(header_after_marker.substr(0, end));
}

// Parse one of the two range halves in "@@ -X,Y +A,B @@".
// `body` is e.g. "10,5" or just "10" (which means count=1).
[[nodiscard]] std::pair<std::int32_t, std::int32_t> parse_hunk_range(std::string_view body,
                                                                     std::string_view full_line) {
    const auto comma = body.find(',');
    std::string_view start_str = body;
    std::string_view count_str = "1";
    if (comma != std::string_view::npos) {
        start_str = body.substr(0, comma);
        count_str = body.substr(comma + 1);
    }

    std::int32_t start = 0;
    std::int32_t count = 0;
    if (auto [_, ec1] =
            std::from_chars(start_str.data(), start_str.data() + start_str.size(), start);
        ec1 != std::errc{}) {
        throw std::runtime_error(fmt::format("malformed hunk header (bad start): {}", full_line));
    }
    if (auto [_, ec2] =
            std::from_chars(count_str.data(), count_str.data() + count_str.size(), count);
        ec2 != std::errc{}) {
        throw std::runtime_error(fmt::format("malformed hunk header (bad count): {}", full_line));
    }
    return {start, count};
}

// Parse "@@ -X,Y +A,B @@ optional comment" into a Hunk's range
// fields. The body lines are filled in by the caller as it iterates
// the rest of the input.
[[nodiscard]] Hunk parse_hunk_header(std::string_view line) {
    if (!line.starts_with("@@")) {
        throw std::runtime_error(fmt::format("expected hunk header, got: {}", line));
    }
    if (line.starts_with("@@@")) {
        throw std::runtime_error(fmt::format("combined / 3-way diffs are not supported: {}", line));
    }

    // Skip "@@ "
    auto cursor = line.find('-');
    if (cursor == std::string_view::npos) {
        throw std::runtime_error(fmt::format("hunk header missing '-': {}", line));
    }
    cursor += 1;
    const auto plus = line.find('+', cursor);
    if (plus == std::string_view::npos) {
        throw std::runtime_error(fmt::format("hunk header missing '+': {}", line));
    }
    const auto old_body = line.substr(cursor, plus - cursor);
    // Trim trailing whitespace on the "-X,Y " body.
    std::size_t old_end = old_body.size();
    while (old_end > 0 && (old_body[old_end - 1] == ' ' || old_body[old_end - 1] == '\t')) {
        --old_end;
    }
    const auto old_trimmed = old_body.substr(0, old_end);

    cursor = plus + 1;
    const auto trail_end = line.find("@@", cursor);
    if (trail_end == std::string_view::npos) {
        throw std::runtime_error(fmt::format("hunk header missing closing @@: {}", line));
    }
    const auto new_body = line.substr(cursor, trail_end - cursor);
    std::size_t new_end = new_body.size();
    while (new_end > 0 && (new_body[new_end - 1] == ' ' || new_body[new_end - 1] == '\t')) {
        --new_end;
    }
    const auto new_trimmed = new_body.substr(0, new_end);

    Hunk h;
    std::tie(h.old_start, h.old_count) = parse_hunk_range(old_trimmed, line);
    std::tie(h.new_start, h.new_count) = parse_hunk_range(new_trimmed, line);
    return h;
}

// True when this line, found after a "+++" header, starts a hunk
// rather than continuing the file body / wrapping prose.
[[nodiscard]] bool is_hunk_header(std::string_view line) {
    return line.starts_with("@@");
}

// True when the line opens a new file diff block.
[[nodiscard]] bool is_old_path_header(std::string_view line) {
    return line.starts_with("--- ");
}

}  // namespace

Patch parse_unified_diff(std::string_view text) {
    Patch patch;
    std::size_t cursor = 0;

    while (cursor < text.size()) {
        std::string_view line;
        if (!next_line(text, cursor, line)) {
            break;
        }

        // Skip prose until we find a "--- " marker. We do not abort
        // on unrecognized lines; LLMs frequently mix explanatory
        // text with the diff.
        if (!is_old_path_header(line)) {
            // "Binary files ... differ" is the one prose-shaped
            // line we refuse to ignore, because silently dropping
            // a binary change is worse than failing.
            if (line.starts_with("Binary files ")) {
                throw std::runtime_error(fmt::format("binary diffs are not supported: {}", line));
            }
            continue;
        }

        FileDiff file;
        file.old_path = parse_path_token(line.substr(4));

        // The next non-blank line should be "+++ <new path>".
        std::string_view new_line;
        if (!next_line(text, cursor, new_line) || !new_line.starts_with("+++ ")) {
            throw std::runtime_error(
                fmt::format("expected '+++' header after '---', got: {}", new_line));
        }
        file.new_path = parse_path_token(new_line.substr(4));

        if (file.old_path == "/dev/null") {
            file.is_new_file = true;
            file.old_path.clear();
        }
        if (file.new_path == "/dev/null") {
            file.is_deleted = true;
            file.new_path.clear();
        }

        // Read hunks until we hit something that is neither a hunk
        // continuation nor a hunk header. Multiple hunks per file
        // are joined by another @@ line.
        while (cursor < text.size()) {
            const std::size_t saved_cursor = cursor;
            std::string_view hdr;
            if (!next_line(text, cursor, hdr)) {
                break;
            }
            if (!is_hunk_header(hdr)) {
                // Either start of a new file diff or wrap-up prose.
                // Rewind so the outer loop sees this line.
                cursor = saved_cursor;
                break;
            }

            Hunk hunk = parse_hunk_header(hdr);

            // Read body lines until we have consumed old_count
            // old-side lines and new_count new-side lines.
            std::int32_t old_seen = 0;
            std::int32_t new_seen = 0;
            while (old_seen < hunk.old_count || new_seen < hunk.new_count) {
                std::string_view body_line;
                if (!next_line(text, cursor, body_line)) {
                    throw std::runtime_error(
                        fmt::format("hunk body truncated at line {} of file {}",
                                    hunk.old_start + old_seen,
                                    file.old_path.empty() ? file.new_path : file.old_path));
                }
                if (body_line.empty()) {
                    // A literally empty line is treated as a context
                    // line of zero characters: ' ' prefix implied.
                    hunk.lines.emplace_back(" ");
                    ++old_seen;
                    ++new_seen;
                    continue;
                }
                const char prefix = body_line.front();
                switch (prefix) {
                    case ' ':
                        ++old_seen;
                        ++new_seen;
                        hunk.lines.emplace_back(body_line);
                        break;
                    case '-':
                        ++old_seen;
                        hunk.lines.emplace_back(body_line);
                        break;
                    case '+':
                        ++new_seen;
                        hunk.lines.emplace_back(body_line);
                        break;
                    case '\\':
                        // "\ No newline at end of file" — record but
                        // do not advance counts.
                        hunk.lines.emplace_back(body_line);
                        break;
                    default:
                        // Unrecognized prefix inside a hunk that has
                        // not yet been fully consumed is a parse
                        // error; that is how we catch off-by-one
                        // bugs in LLM-emitted hunk counts.
                        throw std::runtime_error(fmt::format(
                            "unexpected line in hunk body (prefix '{}'): {}", prefix, body_line));
                }
            }

            file.hunks.push_back(std::move(hunk));
        }

        if (file.hunks.empty() && !file.is_new_file && !file.is_deleted) {
            // A "--- / +++" header pair with no hunks describes no
            // change. We tolerate it (some tools emit it for renames
            // / mode-only changes) by simply dropping the entry.
            continue;
        }

        patch.files.push_back(std::move(file));
    }

    return patch;
}

}  // namespace vectra::exec
