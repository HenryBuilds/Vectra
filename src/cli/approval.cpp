// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "approval.hpp"

#include <cctype>
#include <istream>
#include <ostream>
#include <string>

namespace vectra::cli {

namespace {

[[nodiscard]] std::string trim_lower(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    std::string out;
    out.reserve(end - start);
    for (std::size_t i = start; i < end; ++i) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(s[i]))));
    }
    return out;
}

}  // namespace

std::optional<ApprovalDecision> parse_decision(std::string_view line) noexcept {
    const auto t = trim_lower(line);
    if (t == "y" || t == "yes") {
        return ApprovalDecision::Approve;
    }
    if (t == "n" || t == "no") {
        return ApprovalDecision::Reject;
    }
    if (t == "q" || t == "quit" || t == "exit") {
        return ApprovalDecision::Quit;
    }
    return std::nullopt;
}

std::optional<ApprovalDecision> prompt_decision(std::istream& in, std::ostream& out) {
    std::string line;
    while (true) {
        out << "Apply this patch? [y/n/q] " << std::flush;
        if (!std::getline(in, line)) {
            return std::nullopt;
        }
        if (auto d = parse_decision(line)) {
            return d;
        }
        out << "Please answer y, n, or q.\n";
    }
}

}  // namespace vectra::cli
