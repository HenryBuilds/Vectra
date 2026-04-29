// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Always-Confirm approval prompt. Every patch the LLM proposes runs
// through this gate before vectra::exec::apply_patch touches the
// working tree, so a bad diff cannot scribble over user code without
// an explicit go-ahead.

#pragma once

#include <cstdint>
#include <iosfwd>
#include <optional>
#include <string_view>

namespace vectra::cli {

enum class ApprovalDecision : std::uint8_t {
    Approve,  // y / yes — apply the proposed patch
    Reject,   // n / no  — discard the patch and continue the session
    Quit,     // q / quit — leave the REPL
};

// Pure parser: turn one line of user input into a decision. Returns
// nullopt for unrecognized input so the caller can re-prompt.
[[nodiscard]] std::optional<ApprovalDecision> parse_decision(std::string_view line) noexcept;

// Interactive prompt: write `prompt` to `out`, read a line from `in`,
// and re-prompt on unrecognized input. Returns nullopt only when
// `in` reaches end-of-stream before any valid decision is given —
// the REPL treats that as an implicit Quit.
[[nodiscard]] std::optional<ApprovalDecision> prompt_decision(std::istream& in, std::ostream& out);

}  // namespace vectra::cli
