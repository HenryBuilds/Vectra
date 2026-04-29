// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "repl_command.hpp"

#include <fmt/format.h>

#include <cstddef>
#include <exception>
#include <istream>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/exec/apply.hpp"
#include "vectra/exec/diff.hpp"

#include "approval.hpp"
#include "diff_render.hpp"

namespace vectra::cli {

namespace {

constexpr std::string_view kDefaultSystemPrompt =
    "You are Vectra, a code-editing assistant. When the user asks for a code "
    "change, respond with a single unified diff (--- a/path / +++ b/path / "
    "@@ ... @@). Use repository-relative paths. Wrap the diff in a fenced "
    "block when prose accompanies it. Keep prose short; the user reviews the "
    "diff before it is applied.";

constexpr std::string_view kPrompt = "vectra> ";

// Slash-command sentinel: anything starting with '/' is interpreted
// as a REPL meta-command rather than a model prompt.
[[nodiscard]] bool is_slash_command(std::string_view line) noexcept {
    return !line.empty() && line.front() == '/';
}

[[nodiscard]] std::string_view trim_left(std::string_view s) noexcept {
    std::size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
        ++i;
    }
    return s.substr(i);
}

[[nodiscard]] std::string_view trim(std::string_view s) noexcept {
    s = trim_left(s);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

void print_help(std::ostream& out) {
    out << "Commands:\n"
           "  /help            Show this message.\n"
           "  /history         Show how many turns are in the conversation.\n"
           "  /clear, /reset   Drop the conversation history.\n"
           "  /quit, /exit     Leave the REPL.\n"
           "  Anything else    Send to the model as a turn.\n";
}

// Cap the conversation at `limit` user/assistant pairs by dropping
// the oldest pairs. The system prompt at index 0 is retained. A
// limit of 0 means "no cap".
void prune_history(std::vector<agent::ChatMessage>& history, int limit) {
    if (limit <= 0) {
        return;
    }
    const auto max_size = static_cast<std::size_t>(1 + 2 * limit);
    if (history.size() <= max_size) {
        return;
    }
    const auto excess = history.size() - max_size;
    history.erase(history.begin() + 1, history.begin() + 1 + static_cast<std::ptrdiff_t>(excess));
}

[[nodiscard]] std::string build_streaming_reply(agent::LlmBackend& backend,
                                                std::span<const agent::ChatMessage> messages,
                                                std::ostream& out) {
    agent::GenerateOptions go;
    go.on_token = [&](std::string_view delta) {
        out << delta;
        out.flush();
    };
    auto reply = backend.generate(messages, go);
    out << '\n';
    return reply;
}

// One turn: append the user's input to `history`, stream the model
// reply (also recorded in history), and optionally apply a proposed
// patch. Rejected and applied patches both leave the conversation
// transcript intact so the model can refer back to its previous
// proposal on the user's follow-up. Returns false when the user
// asked to leave the REPL during the approval prompt.
[[nodiscard]] bool run_turn(std::istream& in,
                            std::ostream& out,
                            agent::LlmBackend& backend,
                            const ReplOptions& opts,
                            std::vector<agent::ChatMessage>& history,
                            std::string_view user_input) {
    history.push_back({agent::ChatMessage::Role::User, std::string{user_input}});

    std::string reply;
    try {
        reply = build_streaming_reply(backend, history, out);
    } catch (const std::exception& e) {
        out << fmt::format("\nerror: model request failed: {}\n", e.what());
        // Remove the user message we just appended — the model never
        // saw it through, so the next turn shouldn't either.
        history.pop_back();
        return true;
    }
    history.push_back({agent::ChatMessage::Role::Assistant, reply});

    exec::Patch patch;
    try {
        patch = exec::parse_unified_diff(reply);
    } catch (const std::exception& e) {
        out << fmt::format("\nerror: could not parse diff from reply: {}\n", e.what());
        return true;
    }

    if (patch.empty()) {
        return true;
    }

    out << "\n--- proposed patch ---\n";
    render_diff(out, patch, DiffRenderOptions{opts.use_color});
    out << "----------------------\n";

    const auto decision = prompt_decision(in, out);
    if (!decision || *decision == ApprovalDecision::Quit) {
        return false;
    }
    if (*decision == ApprovalDecision::Reject) {
        out << "patch discarded.\n";
        return true;
    }

    // Approve: apply.
    try {
        exec::ApplyOptions ao;
        ao.repo_root = opts.repo_root;
        const auto result = exec::apply_patch(patch, ao);
        out << fmt::format("applied: {} modified, {} created, {} deleted (backup: {})\n",
                           result.files_modified.size(),
                           result.files_created.size(),
                           result.files_deleted.size(),
                           result.backup_dir.string());
    } catch (const std::exception& e) {
        out << fmt::format("error: apply failed: {}\n", e.what());
    }
    return true;
}

}  // namespace

int run_repl(std::istream& in,
             std::ostream& out,
             agent::LlmBackend& backend,
             const ReplOptions& opts) {
    const std::string_view system_prompt =
        opts.system_prompt.empty() ? kDefaultSystemPrompt : std::string_view{opts.system_prompt};

    std::vector<agent::ChatMessage> history;
    history.push_back({agent::ChatMessage::Role::System, std::string{system_prompt}});

    out << fmt::format("vectra REPL — backend: {}. Type /help for commands, /quit to exit.\n",
                       backend.name());

    std::string line;
    while (true) {
        out << kPrompt << std::flush;
        if (!std::getline(in, line)) {
            out << '\n';
            return 0;
        }
        const auto trimmed = trim(line);
        if (trimmed.empty()) {
            continue;
        }

        if (is_slash_command(trimmed)) {
            if (trimmed == "/quit" || trimmed == "/exit") {
                return 0;
            }
            if (trimmed == "/help") {
                print_help(out);
                continue;
            }
            if (trimmed == "/clear" || trimmed == "/reset") {
                const auto dropped = history.size() - 1;
                history.resize(1);  // keep only the system prompt
                out << fmt::format(
                    "history cleared ({} message{} dropped).\n", dropped, dropped == 1 ? "" : "s");
                continue;
            }
            if (trimmed == "/history") {
                const auto messages = history.size() - 1;  // exclude system
                const auto turns = messages / 2;
                out << fmt::format("history: {} turn{}, {} message{}.\n",
                                   turns,
                                   turns == 1 ? "" : "s",
                                   messages,
                                   messages == 1 ? "" : "s");
                continue;
            }
            out << fmt::format("unknown command: {} (try /help)\n", trimmed);
            continue;
        }

        if (!run_turn(in, out, backend, opts, history, trimmed)) {
            return 0;
        }
        prune_history(history, opts.history_limit);
    }
}

}  // namespace vectra::cli
