// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "diff_render.hpp"

#include <fmt/format.h>

#include <string_view>

namespace vectra::cli {

namespace {

constexpr std::string_view kRed = "\x1b[31m";
constexpr std::string_view kGreen = "\x1b[32m";
constexpr std::string_view kBoldWhite = "\x1b[1;37m";
constexpr std::string_view kDim = "\x1b[2m";
constexpr std::string_view kReset = "\x1b[0m";

// Strip the prefix character ('+', '-', ' ', '\\') from a hunk line
// so we can re-emit it with our own coloring.
[[nodiscard]] std::string_view body(std::string_view line) noexcept {
    return line.empty() ? line : line.substr(1);
}

[[nodiscard]] std::string_view header_path(const exec::FileDiff& f, bool is_old) {
    if (is_old) {
        return f.is_new_file ? std::string_view{"/dev/null"} : std::string_view{f.old_path};
    }
    return f.is_deleted ? std::string_view{"/dev/null"} : std::string_view{f.new_path};
}

void emit(std::ostream& out, std::string_view color, std::string_view text, bool use_color) {
    if (use_color) {
        out << color << text << kReset;
    } else {
        out << text;
    }
}

void render_hunk(std::ostream& out, const exec::Hunk& h, bool use_color) {
    const std::string header =
        fmt::format("@@ -{},{} +{},{} @@", h.old_start, h.old_count, h.new_start, h.new_count);
    emit(out, kDim, header, use_color);
    out << '\n';

    for (const auto& line : h.lines) {
        if (line.empty()) {
            out << '\n';
            continue;
        }
        const char kind = line.front();
        std::string_view text = body(line);
        std::string_view color = kReset;
        if (kind == '+') {
            color = kGreen;
        } else if (kind == '-') {
            color = kRed;
        }
        if (use_color && (kind == '+' || kind == '-')) {
            out << color << kind << text << kReset << '\n';
        } else {
            out << kind << text << '\n';
        }
    }
}

}  // namespace

void render_diff(std::ostream& out, const exec::Patch& patch, const DiffRenderOptions& opts) {
    for (const auto& f : patch.files) {
        const auto from = header_path(f, /*is_old=*/true);
        const auto to = header_path(f, /*is_old=*/false);
        emit(out, kBoldWhite, fmt::format("--- a/{}", from), opts.use_color);
        out << '\n';
        emit(out, kBoldWhite, fmt::format("+++ b/{}", to), opts.use_color);
        out << '\n';
        for (const auto& h : f.hunks) {
            render_hunk(out, h, opts.use_color);
        }
    }
}

}  // namespace vectra::cli
