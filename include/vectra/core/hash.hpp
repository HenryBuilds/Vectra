// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Blake3 content hash type used as the leaf-level identity in the
// Merkle index. Blake3 is chosen over SHA-256 because it is
// substantially faster on the chunk sizes we hash (a few hundred
// bytes to a few kilobytes), and over xxh3 because we want a
// cryptographic hash so a future content-addressed store cannot be
// confused by collisions.

#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace vectra::core {

// Fixed-size 256-bit Blake3 digest. We store the raw bytes; hex is
// only generated when serializing to SQLite or logs.
struct Blake3Hash {
    std::array<uint8_t, 32> bytes{};

    // Lowercase hex, 64 characters. No 0x prefix.
    [[nodiscard]] std::string to_hex() const;

    // Parse a 64-character lowercase or uppercase hex string. Returns
    // a zero-initialized hash if the input is malformed; callers that
    // need strict validation should check the length first.
    [[nodiscard]] static Blake3Hash from_hex(std::string_view hex);

    bool operator==(const Blake3Hash&) const = default;
};

// Hash an arbitrary byte range.
[[nodiscard]] Blake3Hash hash_bytes(std::span<const uint8_t> data);

// Hash a string. Convenience overload — the bytes used are the
// string's UTF-8 representation, which is what the rest of the
// pipeline assumes.
[[nodiscard]] Blake3Hash hash_string(std::string_view s);

}  // namespace vectra::core
