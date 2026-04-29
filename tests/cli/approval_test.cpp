// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "approval.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>

using vectra::cli::ApprovalDecision;
using vectra::cli::parse_decision;
using vectra::cli::prompt_decision;

TEST_CASE("parse_decision recognizes the canonical short forms", "[approval]") {
    REQUIRE(parse_decision("y") == ApprovalDecision::Approve);
    REQUIRE(parse_decision("n") == ApprovalDecision::Reject);
    REQUIRE(parse_decision("q") == ApprovalDecision::Quit);
}

TEST_CASE("parse_decision recognizes the long forms case-insensitively", "[approval]") {
    REQUIRE(parse_decision("YES") == ApprovalDecision::Approve);
    REQUIRE(parse_decision("No") == ApprovalDecision::Reject);
    REQUIRE(parse_decision("Quit") == ApprovalDecision::Quit);
    REQUIRE(parse_decision("EXIT") == ApprovalDecision::Quit);
}

TEST_CASE("parse_decision tolerates surrounding whitespace", "[approval]") {
    REQUIRE(parse_decision("  y\t") == ApprovalDecision::Approve);
    REQUIRE(parse_decision("\n n \r") == ApprovalDecision::Reject);
}

TEST_CASE("parse_decision returns nullopt for unrecognized input", "[approval]") {
    REQUIRE_FALSE(parse_decision("maybe").has_value());
    REQUIRE_FALSE(parse_decision("").has_value());
    REQUIRE_FALSE(parse_decision("ye").has_value());
}

TEST_CASE("prompt_decision returns the first valid answer", "[approval]") {
    std::istringstream in("y\n");
    std::ostringstream out;
    const auto d = prompt_decision(in, out);
    REQUIRE(d == ApprovalDecision::Approve);
    REQUIRE(out.str().find("Apply this patch?") != std::string::npos);
}

TEST_CASE("prompt_decision re-prompts on invalid input", "[approval]") {
    std::istringstream in("huh\nmaybe\nn\n");
    std::ostringstream out;
    const auto d = prompt_decision(in, out);
    REQUIRE(d == ApprovalDecision::Reject);
    // Two retries means the "Please answer" hint shows up at least twice.
    const auto& s = out.str();
    std::size_t hits = 0;
    std::size_t pos = 0;
    while ((pos = s.find("Please answer", pos)) != std::string::npos) {
        ++hits;
        ++pos;
    }
    REQUIRE(hits == 2);
}

TEST_CASE("prompt_decision returns nullopt when stdin closes with no answer", "[approval]") {
    std::istringstream in("");  // empty: getline returns false immediately
    std::ostringstream out;
    const auto d = prompt_decision(in, out);
    REQUIRE_FALSE(d.has_value());
}
