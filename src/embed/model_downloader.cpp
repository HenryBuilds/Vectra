// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "model_downloader.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <system_error>

// cpp-httplib's SSLClient is gated behind this macro. The vcpkg
// port with the [openssl] feature defines it for us, but we set
// it explicitly here as a safety net in case the package config
// changes.
#ifndef CPPHTTPLIB_OPENSSL_SUPPORT
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <httplib.h>
#include <openssl/evp.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vectra::embed::detail {

namespace {

struct ParsedUrl {
    std::string scheme;
    std::string host;
    int port = 0;  // 0 → default for scheme
    std::string path;
};

[[nodiscard]] ParsedUrl parse_url(std::string_view url) {
    ParsedUrl p;
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        throw std::runtime_error(fmt::format("malformed URL: '{}'", url));
    }
    p.scheme = std::string(url.substr(0, scheme_end));

    const auto rest = url.substr(scheme_end + 3);
    const auto path_start = rest.find('/');
    const auto authority =
        (path_start == std::string_view::npos) ? rest : rest.substr(0, path_start);
    p.path = (path_start == std::string_view::npos) ? "/" : std::string(rest.substr(path_start));

    const auto colon = authority.find(':');
    if (colon == std::string_view::npos) {
        p.host = std::string(authority);
    } else {
        p.host = std::string(authority.substr(0, colon));
        p.port = std::atoi(std::string(authority.substr(colon + 1)).c_str());
    }
    return p;
}

[[nodiscard]] std::string sha256_file_hex(const std::filesystem::path& path) {
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (ctx == nullptr) {
        throw std::runtime_error("EVP_MD_CTX_new failed");
    }
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestInit_ex failed");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error(fmt::format("cannot open {} for hashing", path.string()));
    }

    constexpr std::size_t kBufSize = 64 * 1024;
    std::array<char, kBufSize> buf{};
    while (in.good()) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0) {
            if (EVP_DigestUpdate(ctx, buf.data(), static_cast<std::size_t>(n)) != 1) {
                EVP_MD_CTX_free(ctx);
                throw std::runtime_error("EVP_DigestUpdate failed");
            }
        }
    }

    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_len = 0;
    if (EVP_DigestFinal_ex(ctx, digest.data(), &digest_len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("EVP_DigestFinal_ex failed");
    }
    EVP_MD_CTX_free(ctx);

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(digest_len) * 2);
    for (unsigned int i = 0; i < digest_len; ++i) {
        out.push_back(kHex[(digest[i] >> 4) & 0x0F]);
        out.push_back(kHex[digest[i] & 0x0F]);
    }
    return out;
}

}  // namespace

void download_to_file(const DownloadOptions& opts) {
    namespace fs = std::filesystem;

    if (opts.destination.empty()) {
        throw std::runtime_error("download_to_file: destination is empty");
    }

    fs::create_directories(opts.destination.parent_path());

    // Stream into a sibling .part file so a half-finished download
    // never appears at the destination path. We rename atomically on
    // success.
    const auto tmp_path = opts.destination.string() + ".part";

    const ParsedUrl url = parse_url(opts.url);
    if (url.scheme != "https") {
        throw std::runtime_error(
            fmt::format("model downloader only supports https URLs; got scheme '{}'", url.scheme));
    }

    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(fmt::format("cannot open {} for writing", tmp_path));
    }

    httplib::SSLClient client(url.host, url.port == 0 ? 443 : url.port);
    client.set_follow_location(true);
    client.enable_server_certificate_verification(true);
    client.set_read_timeout(120, 0);
    client.set_connection_timeout(30, 0);
    client.set_default_headers({{"User-Agent", "vectra/" VECTRA_VERSION}});

    std::size_t downloaded = 0;
    std::size_t total = opts.expected_size_bytes;

    auto result = client.Get(
        url.path,
        // Response handler — pull Content-Length out of the headers
        // before any chunks arrive so the progress display has a
        // total to compare against.
        [&](const httplib::Response& resp) {
            const auto cl = resp.get_header_value("Content-Length");
            if (!cl.empty()) {
                try {
                    total = static_cast<std::size_t>(std::stoull(cl));
                } catch (...) {
                    // Malformed header — just leave `total` as the
                    // configured fallback.
                }
            }
            return true;
        },
        // Content receiver — write each chunk to disk and tick the
        // progress callback. Returning false aborts the transfer.
        [&](const char* data, std::size_t length) {
            out.write(data, static_cast<std::streamsize>(length));
            if (!out) {
                return false;
            }
            downloaded += length;
            if (opts.on_progress) {
                opts.on_progress(downloaded, total);
            }
            return true;
        });

    out.close();

    if (!result) {
        fs::remove(tmp_path);
        throw std::runtime_error(fmt::format("download failed for {} (httplib error {})",
                                             opts.url,
                                             static_cast<int>(result.error())));
    }
    if (result->status < 200 || result->status >= 300) {
        fs::remove(tmp_path);
        throw std::runtime_error(fmt::format("HTTP {} downloading {}", result->status, opts.url));
    }

    if (!opts.expected_sha256.empty()) {
        const auto actual = sha256_file_hex(tmp_path);
        if (actual != opts.expected_sha256) {
            fs::remove(tmp_path);
            throw std::runtime_error(fmt::format("sha256 mismatch for {}: expected {} but got {}",
                                                 opts.url,
                                                 opts.expected_sha256,
                                                 actual));
        }
    } else {
        spdlog::warn("no expected_sha256 supplied for {}; download is unverified",
                     opts.destination.string());
    }

    // Atomic-ish rename. If destination and tmp live on different
    // filesystems we fall back to copy+remove.
    std::error_code ec;
    fs::remove(opts.destination, ec);  // best-effort if a stale file is there
    ec.clear();
    fs::rename(tmp_path, opts.destination, ec);
    if (ec) {
        std::error_code copy_ec;
        fs::copy_file(tmp_path, opts.destination, fs::copy_options::overwrite_existing, copy_ec);
        fs::remove(tmp_path);
        if (copy_ec) {
            throw std::runtime_error(
                fmt::format("could not move downloaded file into place: {}", copy_ec.message()));
        }
    }
}

}  // namespace vectra::embed::detail
