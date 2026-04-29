// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/hash.hpp"

#include <blake3.h>

#include <array>
#include <cstring>

namespace vectra::core {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

[[nodiscard]] int hex_value(char c) noexcept {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

}  // namespace

std::string Blake3Hash::to_hex() const {
    std::string out(64, '\0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        out[i * 2] = kHexDigits[(bytes[i] >> 4) & 0x0F];
        out[i * 2 + 1] = kHexDigits[bytes[i] & 0x0F];
    }
    return out;
}

Blake3Hash Blake3Hash::from_hex(std::string_view hex) {
    Blake3Hash h{};
    if (hex.size() != 64) {
        return h;
    }
    for (std::size_t i = 0; i < h.bytes.size(); ++i) {
        const int hi = hex_value(hex[i * 2]);
        const int lo = hex_value(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return Blake3Hash{};  // any malformed nibble → zero hash
        }
        h.bytes[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return h;
}

Blake3Hash hash_bytes(std::span<const uint8_t> data) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, data.data(), data.size());

    Blake3Hash out{};
    blake3_hasher_finalize(&hasher, out.bytes.data(), out.bytes.size());
    return out;
}

Blake3Hash hash_string(std::string_view s) {
    return hash_bytes(std::span{reinterpret_cast<const uint8_t*>(s.data()), s.size()});
}

}  // namespace vectra::core
