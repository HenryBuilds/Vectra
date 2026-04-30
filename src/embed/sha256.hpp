// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Single-file SHA-256 implementation (FIPS 180-4). Header-only,
// no external dependencies. Used by the model downloader to verify
// HuggingFace downloads when an expected hash is published.
//
// We vendor this rather than link against OpenSSL because vcpkg's
// openssl port is fragile on macOS arm64, and SHA-256 is the only
// cryptographic primitive we actually need anywhere in the project.

#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace vectra::embed::detail {

class Sha256 {
public:
    Sha256() noexcept { reset(); }

    void reset() noexcept {
        state_ = {0x6a09e667U,
                  0xbb67ae85U,
                  0x3c6ef372U,
                  0xa54ff53aU,
                  0x510e527fU,
                  0x9b05688cU,
                  0x1f83d9abU,
                  0x5be0cd19U};
        bitlen_ = 0;
        buflen_ = 0;
    }

    void update(const void* data, std::size_t len) noexcept {
        const auto* p = static_cast<const std::uint8_t*>(data);
        while (len > 0) {
            const std::size_t take = std::min(len, std::size_t{64} - buflen_);
            std::memcpy(buffer_.data() + buflen_, p, take);
            buflen_ += take;
            p += take;
            len -= take;
            if (buflen_ == 64) {
                transform(buffer_.data());
                bitlen_ += 512;
                buflen_ = 0;
            }
        }
    }

    [[nodiscard]] std::array<std::uint8_t, 32> finalize() noexcept {
        // Append the standard padding: a single 1-bit, then zeros, then
        // the original message length in bits as a 64-bit big-endian
        // integer. The block layout means we may need either one or
        // two more 64-byte blocks.
        const std::uint64_t total_bits = bitlen_ + std::uint64_t{buflen_} * 8U;
        buffer_[buflen_++] = 0x80U;
        if (buflen_ > 56) {
            std::memset(buffer_.data() + buflen_, 0, 64U - buflen_);
            transform(buffer_.data());
            buflen_ = 0;
        }
        std::memset(buffer_.data() + buflen_, 0, 56U - buflen_);
        for (int i = 0; i < 8; ++i) {
            buffer_[63 - static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>(total_bits >> (i * 8));
        }
        transform(buffer_.data());

        std::array<std::uint8_t, 32> out{};
        for (std::size_t i = 0; i < 8; ++i) {
            out[i * 4 + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
            out[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
            out[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
            out[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
        }
        return out;
    }

private:
    [[nodiscard]] static constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
        return (x >> n) | (x << (32 - n));
    }

    void transform(const std::uint8_t* block) noexcept {
        static constexpr std::array<std::uint32_t, 64> k = {
            0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
            0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
            0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
            0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
            0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
            0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
            0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
            0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
            0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
            0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
            0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U,
        };

        std::array<std::uint32_t, 64> w{};
        for (std::size_t i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(block[i * 4 + 0]) << 24) |
                   (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
                   (static_cast<std::uint32_t>(block[i * 4 + 3]));
        }
        for (std::size_t i = 16; i < 64; ++i) {
            const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }

        std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
        std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];

        for (std::size_t i = 0; i < 64; ++i) {
            const std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            const std::uint32_t ch = (e & f) ^ (~e & g);
            const std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
            const std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = S0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::uint64_t bitlen_ = 0;
    std::size_t buflen_ = 0;
};

[[nodiscard]] inline std::string sha256_file_hex(const std::filesystem::path& path) {
    Sha256 h;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open file for hashing: " + path.string());
    }
    std::array<char, 64 * 1024> buf{};
    while (in.good()) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0) {
            h.update(buf.data(), static_cast<std::size_t>(n));
        }
    }
    const auto digest = h.finalize();

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (auto byte : digest) {
        out.push_back(kHex[(byte >> 4) & 0x0F]);
        out.push_back(kHex[byte & 0x0F]);
    }
    return out;
}

}  // namespace vectra::embed::detail
