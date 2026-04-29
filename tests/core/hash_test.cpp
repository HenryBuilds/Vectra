// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/hash.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using vectra::core::Blake3Hash;
using vectra::core::hash_string;

TEST_CASE("Blake3 hash of empty string matches the published vector", "[hash]") {
    // Empty-input vector from the reference Blake3 spec.
    const auto h = hash_string("");
    REQUIRE(h.to_hex() == "af1349b9f5f9a1a6a0404dea36dcc9499bcb25c9adc112b7cc9a93cae41f3262");
}

TEST_CASE("Blake3 hash of 'abc' matches the published vector", "[hash]") {
    // Three-byte vector from the reference Blake3 spec.
    const auto h = hash_string("abc");
    REQUIRE(h.to_hex() == "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85");
}

TEST_CASE("hex round-trip preserves the digest", "[hash]") {
    const auto original = hash_string("the quick brown fox jumps over the lazy dog");
    const auto restored = Blake3Hash::from_hex(original.to_hex());
    REQUIRE(original == restored);
}

TEST_CASE("from_hex rejects malformed input by returning zero hash", "[hash]") {
    const Blake3Hash zero{};

    SECTION("wrong length") {
        REQUIRE(Blake3Hash::from_hex("deadbeef") == zero);
    }
    SECTION("non-hex characters") {
        std::string bad(64, 'z');
        REQUIRE(Blake3Hash::from_hex(bad) == zero);
    }
}

TEST_CASE("identical inputs hash to identical digests", "[hash]") {
    const auto a = hash_string("vectra");
    const auto b = hash_string("vectra");
    REQUIRE(a == b);
}

TEST_CASE("different inputs produce different digests", "[hash]") {
    const auto a = hash_string("vectra");
    const auto b = hash_string("vectrA");  // single-char change
    REQUIRE_FALSE(a == b);
}
