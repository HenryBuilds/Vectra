// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// HTTPS download helper with progress reporting and optional SHA256
// verification. Internal to vectra-embed; consumed by the model
// CLI command.

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

namespace vectra::embed::detail {

struct DownloadOptions {
    // Full HTTPS URL. Redirects are followed transparently.
    std::string url;

    // Final destination on disk. The parent directory is created if
    // it does not exist. The file is written to a sibling temp file
    // first and atomically renamed into place on success, so a
    // partially downloaded file never appears at the final path.
    std::filesystem::path destination;

    // Lowercase hex SHA256 of the expected payload. Empty disables
    // verification (with a warning logged).
    std::string expected_sha256;

    // Used purely for the progress display when the server does not
    // send a Content-Length header. 0 means "unknown size".
    std::size_t expected_size_bytes = 0;

    // Called periodically with (downloaded, total). `total` is 0
    // when the size is unknown. The callback is invoked from the
    // download thread, may block briefly, and must not throw.
    std::function<void(std::size_t downloaded, std::size_t total)> on_progress;
};

// Download `opts.url` into `opts.destination`. Throws std::runtime_error
// on transport failure, hash mismatch, or filesystem error.
void download_to_file(const DownloadOptions& opts);

}  // namespace vectra::embed::detail
