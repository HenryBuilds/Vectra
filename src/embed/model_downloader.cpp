// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "model_downloader.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <system_error>

#include "sha256.hpp"

#ifdef _WIN32
#define VECTRA_POPEN _popen
#define VECTRA_PCLOSE _pclose
#else
#include <sys/wait.h>
#define VECTRA_POPEN popen
#define VECTRA_PCLOSE pclose
#endif

namespace vectra::embed::detail {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string shell_quote(std::string_view s) {
    // We feed filesystem paths and an https:// URL we just typed; both
    // are within a controlled set. Wrap in double quotes so spaces in
    // paths survive the shell, and reject embedded quote characters
    // out of paranoia rather than try to escape them.
    if (s.find('"') != std::string_view::npos) {
        throw std::runtime_error(
            fmt::format("model downloader refuses path / URL with embedded quote: '{}'", s));
    }
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    out += s;
    out += '"';
    return out;
}

[[nodiscard]] int normalize_status(int popen_status) noexcept {
#ifdef _WIN32
    return popen_status;
#else
    if (WIFEXITED(popen_status)) {
        return WEXITSTATUS(popen_status);
    }
    return -1;
#endif
}

// Run `curl` to download `url` to `dest`. We delegate to the system
// curl rather than embed an HTTPS client because every supported
// platform ships curl out of the box (Windows 10+, macOS, every
// Linux distro), and using it sidesteps vcpkg's openssl flake on
// macOS arm64. Output goes to stderr (curl's progress meter) so
// the caller sees a familiar progress display; stdout is consumed
// for protocol reasons but discarded.
void run_curl(const std::string& url, const fs::path& dest, const std::string& user_agent) {
    std::string cmd = "curl --location --fail --silent --show-error --progress-bar";
    cmd += " --user-agent ";
    cmd += shell_quote(user_agent);
    cmd += " --output ";
    cmd += shell_quote(dest.string());
    cmd += ' ';
    cmd += shell_quote(url);

    // We let curl write to its own stderr (where the progress meter
    // and any error messages naturally go) and just check its exit
    // code. std::system is sufficient — we don't need to capture
    // stdout, and curl's exit codes are well documented.
    const int status = std::system(cmd.c_str());
    const int rc = normalize_status(status);
    if (rc != 0) {
        throw std::runtime_error(fmt::format(
            "curl failed for {} (exit code {}). Is curl installed and on your PATH?", url, rc));
    }
}

}  // namespace

void download_to_file(const DownloadOptions& opts) {
    if (opts.destination.empty()) {
        throw std::runtime_error("download_to_file: destination is empty");
    }
    if (opts.url.empty()) {
        throw std::runtime_error("download_to_file: url is empty");
    }
    if (opts.url.rfind("https://", 0) != 0) {
        throw std::runtime_error(
            fmt::format("model downloader only supports https URLs; got '{}'", opts.url));
    }

    fs::create_directories(opts.destination.parent_path());

    // Stream into a sibling .part file so a half-finished download
    // never appears at the destination path. We rename atomically on
    // success.
    const auto tmp_path = fs::path{opts.destination.string() + ".part"};

    const std::string user_agent = "vectra/" VECTRA_VERSION;

    try {
        run_curl(opts.url, tmp_path, user_agent);
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp_path, ec);
        throw;
    }

    if (opts.on_progress) {
        // curl owns the progress display, but the public API still
        // promises one final tick when the transfer is complete so
        // callers can wrap up status lines, set indicators, etc.
        std::error_code ec;
        const auto size = fs::file_size(tmp_path, ec);
        if (!ec) {
            opts.on_progress(static_cast<std::size_t>(size), static_cast<std::size_t>(size));
        }
    }

    if (!opts.expected_sha256.empty()) {
        const auto actual = sha256_file_hex(tmp_path);
        if (actual != opts.expected_sha256) {
            std::error_code ec;
            fs::remove(tmp_path, ec);
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
        std::error_code rm_ec;
        fs::remove(tmp_path, rm_ec);
        if (copy_ec) {
            throw std::runtime_error(
                fmt::format("could not move downloaded file into place: {}", copy_ec.message()));
        }
    }
}

}  // namespace vectra::embed::detail
