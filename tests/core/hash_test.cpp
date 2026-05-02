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

TEST_CASE("hash output is exactly 64 hex chars (32 bytes)", "[hash]") {
    REQUIRE(hash_string("anything").to_hex().size() == 64);
    REQUIRE(hash_string("").to_hex().size() == 64);
    // Long input — output length must still be the fixed 32-byte
    // Blake3 digest, regardless of input size.
    REQUIRE(hash_string(std::string(1'000'000, 'x')).to_hex().size() == 64);
}

TEST_CASE("hash of a single byte is stable across calls", "[hash]") {
    // Repeatability check: hashing produces a deterministic result,
    // not a function of mtime, randomness, or any global state.
    const auto first = hash_string("x").to_hex();
    for (int i = 0; i < 10; ++i) {
        REQUIRE(hash_string("x").to_hex() == first);
    }
}

TEST_CASE("hash handles binary input including embedded nulls", "[hash]") {
    // chunker output can contain arbitrary bytes from a source file
    // (UTF-8, BOM, null bytes from string literals). The hash must
    // not stop at the first null.
    const std::string a("abc\0def", 7);
    const std::string b("abc\0DEF", 7);  // differs after the null
    REQUIRE_FALSE(hash_string(a) == hash_string(b));
}

TEST_CASE("from_hex accepts both uppercase and lowercase hex", "[hash]") {
    const auto orig = hash_string("vectra");
    const auto hex_lower = orig.to_hex();
    std::string hex_upper = hex_lower;
    for (auto& c : hex_upper) {
        if (c >= 'a' && c <= 'f')
            c = static_cast<char>(c - 'a' + 'A');
    }
    REQUIRE(Blake3Hash::from_hex(hex_lower) == orig);
    REQUIRE(Blake3Hash::from_hex(hex_upper) == orig);
}

TEST_CASE("from_hex rejects an empty string by returning zero hash", "[hash]") {
    const Blake3Hash zero{};
    REQUIRE(Blake3Hash::from_hex("") == zero);
}

TEST_CASE("zero-hash compares unequal to a real digest", "[hash]") {
    const Blake3Hash zero{};
    const auto real_one = hash_string("anything");
    REQUIRE_FALSE(zero == real_one);
}
