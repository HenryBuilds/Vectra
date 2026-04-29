// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "model_command.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <stdexcept>

#include "vectra/embed/model_registry.hpp"

// Internal header from src/embed/. Reachable because the CLI's
// CMakeLists adds src/embed/ to the include path; the model command
// needs the downloader directly without going through vectra-embed's
// public surface.
#include "model_downloader.hpp"

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string format_size(std::size_t bytes) {
    constexpr double KB = 1024.0;
    constexpr double MB = KB * 1024.0;
    constexpr double GB = MB * 1024.0;
    const double b = static_cast<double>(bytes);
    if (b >= GB)
        return fmt::format("{:.2f} GB", b / GB);
    if (b >= MB)
        return fmt::format("{:.1f} MB", b / MB);
    if (b >= KB)
        return fmt::format("{:.1f} KB", b / KB);
    return fmt::format("{} B", bytes);
}

// Render an in-place progress line on stderr. Throttled to once per
// 100 ms so we do not spam the terminal.
class ProgressPrinter {
public:
    void update(std::size_t downloaded, std::size_t total) {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_print_ < std::chrono::milliseconds(100) && downloaded < total) {
            return;
        }
        last_print_ = now;

        if (total > 0) {
            const double pct = 100.0 * static_cast<double>(downloaded) / static_cast<double>(total);
            std::fprintf(stderr,
                         "\r  %5.1f%%  %12s / %-12s",
                         pct,
                         format_size(downloaded).c_str(),
                         format_size(total).c_str());
        } else {
            std::fprintf(stderr, "\r  %12s downloaded", format_size(downloaded).c_str());
        }
        std::fflush(stderr);
    }

    void finalize() { std::fprintf(stderr, "\n"); }

private:
    std::chrono::steady_clock::time_point last_print_{};
};

}  // namespace

int run_model_list() {
    fmt::print("{:<22} {:<10} {}\n", "NAME", "DIM", "DESCRIPTION");
    for (const auto& m : embed::ModelRegistry::all()) {
        const auto p = embed::ModelRegistry::local_path(m);
        const auto cache = fs::exists(p) ? "[cached]" : "";
        fmt::print("{:<22} {:<10} {}  {}\n", m.name, m.dim, m.description, cache);
    }
    return 0;
}

int run_model_where(const ModelWhereOptions& opts) {
    const auto* entry = embed::ModelRegistry::by_name(opts.name);
    if (entry == nullptr) {
        fmt::print(stderr, "error: unknown model '{}'. Try `vectra model list`.\n", opts.name);
        return 2;
    }
    fmt::print("{}\n", embed::ModelRegistry::local_path(*entry).string());
    return 0;
}

int run_model_pull(const ModelPullOptions& opts) {
    const auto* entry = embed::ModelRegistry::by_name(opts.name);
    if (entry == nullptr) {
        fmt::print(stderr, "error: unknown model '{}'. Try `vectra model list`.\n", opts.name);
        return 2;
    }

    const auto destination = embed::ModelRegistry::local_path(*entry);
    if (fs::exists(destination) && !opts.force) {
        fmt::print(stderr,
                   "model already cached at {}\n"
                   "use --force to re-download\n",
                   destination.string());
        return 0;
    }

    const auto url = embed::ModelRegistry::download_url(*entry);
    fmt::print(stderr, "downloading {} -> {}\n", url, destination.string());

    ProgressPrinter progress;
    embed::detail::DownloadOptions dl;
    dl.url = url;
    dl.destination = destination;
    dl.expected_sha256 = entry->sha256;
    dl.expected_size_bytes = entry->size_bytes;
    dl.on_progress = [&](std::size_t d, std::size_t t) { progress.update(d, t); };

    try {
        embed::detail::download_to_file(dl);
    } catch (const std::exception& e) {
        progress.finalize();
        fmt::print(stderr, "error: {}\n", e.what());
        return 1;
    }
    progress.finalize();
    fmt::print(stderr, "saved to {}\n", destination.string());
    return 0;
}

}  // namespace vectra::cli
