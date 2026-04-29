// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "index_command.hpp"

#include <fmt/chrono.h>
#include <fmt/format.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#include "vectra/core/chunker.hpp"
#include "vectra/core/hash.hpp"
#include "vectra/core/language.hpp"
#include "vectra/store/store.hpp"

#include "cli_paths.hpp"
#include "walker.hpp"

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

// Walk upward from `start` looking for a directory that contains
// `languages.toml`. This makes the CLI usable from any subdirectory
// of a Vectra checkout during development without needing
// --resources. For installed binaries the caller should pass
// --resources or set VECTRA_RESOURCES.
[[nodiscard]] fs::path search_upward_for_resources(fs::path start) {
    constexpr int kMaxDepth = 16;
    for (int i = 0; i < kMaxDepth; ++i) {
        if (fs::exists(start / "languages.toml")) {
            return start;
        }
        const fs::path parent = start.parent_path();
        if (parent == start) {
            break;
        }
        start = parent;
    }
    return {};
}

// Look in the install-side share/vectra directory next to the
// running binary. Covers both `<prefix>/bin/vectra` +
// `<prefix>/share/vectra/` (Unix-style) and `<exe>/share/vectra/`
// (zip-distribution-style).
[[nodiscard]] fs::path search_install_for_resources() {
    const auto exe = current_exe_path();
    if (exe.empty()) {
        return {};
    }
    const auto exe_dir = exe.parent_path();
    for (const auto& candidate : {
             exe_dir / ".." / "share" / "vectra",
             exe_dir / "share" / "vectra",
         }) {
        std::error_code ec;
        if (fs::exists(candidate / "languages.toml", ec)) {
            return fs::weakly_canonical(candidate, ec);
        }
    }
    return {};
}

[[nodiscard]] fs::path resolve_resources(const fs::path& explicit_path) {
    if (!explicit_path.empty()) {
        return explicit_path;
    }
    if (const char* env = std::getenv("VECTRA_RESOURCES"); env != nullptr && *env != '\0') {
        return env;
    }
    if (auto found = search_upward_for_resources(fs::current_path()); !found.empty()) {
        return found;
    }
    if (auto found = search_install_for_resources(); !found.empty()) {
        return found;
    }
    throw std::runtime_error(
        "could not locate languages.toml. Pass --resources <path>, set VECTRA_RESOURCES, "
        "or run from inside a directory that contains a Vectra checkout.");
}

[[nodiscard]] std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(fmt::format("cannot open {}", path.string()));
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

struct Stats {
    std::size_t files_total = 0;
    std::size_t files_indexed = 0;
    std::size_t files_unchanged = 0;
    std::size_t files_errored = 0;
    std::size_t chunks_added = 0;
};

void print_progress(std::size_t n,
                    std::size_t total,
                    const fs::path& relative,
                    std::size_t chunk_count,
                    std::string_view status) {
    fmt::print(stderr,
               "  [{:>5}/{:>5}]  {:<60}  {:<9}  ({} chunks)\n",
               n,
               total,
               relative.generic_string(),
               status,
               chunk_count);
}

void print_summary(const Stats& s, std::chrono::milliseconds dur) {
    const double seconds = static_cast<double>(dur.count()) / 1000.0;
    const double rate = seconds > 0.0 ? static_cast<double>(s.files_total) / seconds : 0.0;
    fmt::print("\n");
    fmt::print("  files seen:      {}\n", s.files_total);
    fmt::print("  files indexed:   {}\n", s.files_indexed);
    fmt::print("  files unchanged: {}\n", s.files_unchanged);
    fmt::print("  files errored:   {}\n", s.files_errored);
    fmt::print("  chunks added:    {}\n", s.chunks_added);
    fmt::print("  time:            {:.2f}s ({:.0f} files/s)\n", seconds, rate);
}

}  // namespace

int run_index(const IndexOptions& opts) {
    if (opts.root.empty() || !fs::is_directory(opts.root)) {
        fmt::print(stderr, "error: <path> '{}' is not a directory\n", opts.root.string());
        return 2;
    }

    const fs::path resources = resolve_resources(opts.resources);
    fmt::print(stderr, "resources: {}\n", resources.string());

    auto registry = core::LanguageRegistry::from_toml(resources / "languages.toml", resources);
    core::Chunker chunker(registry);

    const fs::path db_path = opts.db.empty() ? opts.root / ".vectra" / "index.db" : opts.db;
    fs::create_directories(db_path.parent_path());
    auto store = store::Store::open(db_path);
    fmt::print(stderr, "database:  {}\n\n", db_path.string());

    const FileWalker walker;
    const auto files = walker.walk(opts.root, registry);

    Stats stats;
    stats.files_total = files.size();

    const auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < files.size(); ++i) {
        const fs::path& abs = files[i];
        const fs::path rel = fs::relative(abs, opts.root);

        std::string content;
        try {
            content = read_file(abs);
        } catch (const std::exception& e) {
            stats.files_errored++;
            fmt::print(stderr, "warn: skip {}: {}\n", rel.generic_string(), e.what());
            continue;
        }

        const auto file_hash = core::hash_string(content).to_hex();
        const auto rel_path = rel.generic_string();
        const auto prior = store.get_file_record(rel_path);

        if (prior && prior->file_blake3 == file_hash) {
            stats.files_unchanged++;
            if (!opts.quiet) {
                print_progress(i + 1, files.size(), rel, 0, "unchanged");
            }
            continue;
        }

        const core::Language* lang = registry.for_path(abs);
        if (lang == nullptr) {
            // The walker already filtered, so this branch is mostly
            // defensive — but if it fires we want to know.
            stats.files_errored++;
            continue;
        }

        std::vector<core::Chunk> chunks;
        try {
            chunks = chunker.chunk(content, *lang);
        } catch (const std::exception& e) {
            stats.files_errored++;
            fmt::print(stderr, "warn: chunker failed on {}: {}\n", rel_path, e.what());
            continue;
        }

        try {
            store.delete_chunks_for_file(rel_path);
            store.put_chunks(rel_path, chunks);
            store.put_file_record(store::FileRecord{
                .path = rel_path,
                .file_blake3 = file_hash,
                .last_indexed_at = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count(),
            });
        } catch (const std::exception& e) {
            stats.files_errored++;
            fmt::print(stderr, "warn: store failed on {}: {}\n", rel_path, e.what());
            continue;
        }

        stats.files_indexed++;
        stats.chunks_added += chunks.size();
        if (!opts.quiet) {
            print_progress(i + 1, files.size(), rel, chunks.size(), "indexed");
        }
    }

    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    print_summary(stats, duration);

    return stats.files_errored == 0 ? 0 : 1;
}

}  // namespace vectra::cli
