// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "claude_subprocess.hpp"

#include <fmt/format.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <ostream>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#define VECTRA_POPEN _popen
#define VECTRA_PCLOSE _pclose
#else
#include <sys/wait.h>
#define VECTRA_POPEN popen
#define VECTRA_PCLOSE pclose
#endif

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path make_tmp_path(std::string_view label) {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto base = fs::temp_directory_path() / "vectra-prompts";
    std::error_code ec;
    fs::create_directories(base, ec);
    return base / (std::string{label} + "-" + session + "-" + std::to_string(counter.fetch_add(1)) +
                   ".txt");
}

[[nodiscard]] std::string shell_quote(std::string_view s) {
    // Tokens go through cmd.exe / /bin/sh. Filesystem paths and
    // simple flag strings are the only inputs we feed; surround
    // with double-quotes when they contain whitespace, otherwise
    // leave bare.
    if (s.find_first_of(" \t") == std::string_view::npos) {
        return std::string{s};
    }
    return fmt::format("\"{}\"", s);
}

[[nodiscard]] int normalize_status(int popen_status) noexcept {
#ifdef _WIN32
    return popen_status;
#else
    if (WIFEXITED(popen_status)) {
        return WEXITSTATUS(popen_status);
    }
    return -1;
#endif
}

}  // namespace

std::string json_escape(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 2);
    for (char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    out += fmt::format("\\u{:04x}", static_cast<unsigned char>(c));
                } else {
                    out += c;
                }
        }
    }
    return out;
}

std::string format_context_event(const std::vector<ContextChunk>& chunks) {
    std::string out;
    out.reserve(64 + chunks.size() * 96);
    out += R"({"type":"vectra_event","subtype":"context","chunks":[)";
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (i > 0) {
            out += ',';
        }
        const auto& c = chunks[i];
        out += fmt::format(
            R"({{"file":"{}","start_line":{},"end_line":{},"symbol":"{}","kind":"{}"}})",
            json_escape(c.file_path),
            c.start_line,
            c.end_line,
            json_escape(c.symbol),
            json_escape(c.kind));
    }
    out += "]}\n";
    return out;
}

std::string compose_prompt(const PromptComposition& comp) {
    std::string out;
    out.reserve(256 + comp.task.size());

    out += "TASK: ";
    out += comp.task;
    out += '\n';

    if (!comp.context.empty()) {
        out += '\n';
        out +=
            "Below are excerpts the local index surfaced as relevant. Treat\n"
            "them as reference snippets only — they may be stale, and Claude\n"
            "Code's Edit / Write tools require a fresh Read tool call on the\n"
            "same file within this session before they accept a modification.\n"
            "Always call Read on a file before editing it, even if the snippet\n"
            "below already shows the lines you intend to change.\n\n";

        for (const auto& c : comp.context) {
            out += fmt::format(
                "<context file=\"{}\" lines=\"{}-{}\"", c.file_path, c.start_line, c.end_line);
            if (!c.symbol.empty()) {
                out += fmt::format(" symbol=\"{}\"", c.symbol);
            }
            if (!c.kind.empty()) {
                out += fmt::format(" kind=\"{}\"", c.kind);
            }
            out += ">\n";
            out += c.text;
            if (out.empty() || out.back() != '\n') {
                out += '\n';
            }
            out += "</context>\n\n";
        }
    }

    return out;
}

TempFile::TempFile(std::string_view label) : path_(make_tmp_path(label)) {}

TempFile::~TempFile() {
    std::error_code ec;
    fs::remove(path_, ec);
}

void TempFile::write(std::string_view content) {
    std::ofstream out(path_, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            fmt::format("could not open temp file for writing: {}", path_.string()));
    }
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!out) {
        throw std::runtime_error(
            fmt::format("could not write prompt to temp file: {}", path_.string()));
    }
}

int run_claude(const ClaudeInvocation& inv, std::ostream& out) {
    if (inv.prompt_file.empty()) {
        throw std::runtime_error("run_claude: prompt_file is required");
    }
    if (inv.claude_binary.empty()) {
        throw std::runtime_error("run_claude: claude_binary is empty");
    }

    // Vectra's contract is "we wrap Claude Code, you keep using the
    // auth you already set up via `claude login`". Claude Code,
    // however, prefers an API key over OAuth as soon as one is
    // present in the environment — and Cursor / Windsurf / various
    // dev tools love to inject ANTHROPIC_API_KEY into their child
    // shells. A stale or empty key then blocks the user's Pro/Max
    // session with "Invalid API key". Clear the variable in the
    // subprocess so claude falls through to its native OAuth path.
    //
    // Users who genuinely want API-key auth for `claude -p` should
    // call claude directly; Vectra's wrapper exists to ride on the
    // user's interactive login.
    std::string cmd;
#ifdef _WIN32
    cmd += "set \"ANTHROPIC_API_KEY=\" && ";
#else
    cmd += "unset ANTHROPIC_API_KEY; ";
#endif
    cmd += shell_quote(inv.claude_binary);
    cmd += " -p";
    for (const auto& a : inv.extra_args) {
        cmd += ' ';
        cmd += shell_quote(a);
    }
    cmd += " < ";
    cmd += shell_quote(inv.prompt_file.string());

    FILE* pipe = VECTRA_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return -1;
    }

    char buffer[4096];
    while (true) {
        const auto n = std::fread(buffer, 1, sizeof(buffer), pipe);
        if (n == 0) {
            break;
        }
        out.write(buffer, static_cast<std::streamsize>(n));
        out.flush();
    }
    return normalize_status(VECTRA_PCLOSE(pipe));
}

}  // namespace vectra::cli
