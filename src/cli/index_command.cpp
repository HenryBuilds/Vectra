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

#if VECTRA_HAS_EMBED
#include "vectra/embed/embedder.hpp"
#include "vectra/embed/model_registry.hpp"
#endif

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
    std::size_t chunks_embedded = 0;
    std::int64_t embed_time_ms = 0;
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
    if (s.chunks_embedded > 0) {
        const double embed_seconds = static_cast<double>(s.embed_time_ms) / 1000.0;
        const double embed_rate =
            embed_seconds > 0.0 ? static_cast<double>(s.chunks_embedded) / embed_seconds : 0.0;
        fmt::print("  chunks embedded: {} ({:.2f}s, {:.0f} chunks/s)\n",
                   s.chunks_embedded,
                   embed_seconds,
                   embed_rate);
    }
    fmt::print("  time:            {:.2f}s ({:.0f} files/s)\n", seconds, rate);
}

#if VECTRA_HAS_EMBED
// Load and prepare an embedder from a registry name. Returns nullptr
// for an empty name (symbol-only indexing); throws on misconfiguration
// (unknown name, model not cached) so the caller can bail before
// chunking starts.
[[nodiscard]] std::unique_ptr<embed::Embedder> open_embedder(const std::string& name) {
    if (name.empty()) {
        return nullptr;
    }
    const auto* entry = embed::ModelRegistry::by_name(name);
    if (entry == nullptr) {
        throw std::runtime_error(fmt::format("unknown model '{}'. Try `vectra model list`.", name));
    }
    const auto model_path = embed::ModelRegistry::local_path(*entry);
    std::error_code ec;
    if (!std::filesystem::exists(model_path, ec)) {
        throw std::runtime_error(
            fmt::format("model not cached. Run `vectra model pull {}` first.", name));
    }
    embed::EmbedderConfig cfg;
    cfg.model_path = model_path;
    cfg.model_id = entry->name;
    return std::make_unique<embed::Embedder>(embed::Embedder::open(cfg));
}

// Embed every chunk that has no vector for `embedder.model_id()` and
// persist via store.put_embedding. Returns (count, wall-clock ms).
//
// Runs after the file-walk pass so the call covers two cases at once:
//   1. First-time embedding on an existing symbol-only index — every
//      chunk is missing.
//   2. Incremental: the just-indexed files added new chunks, the
//      rest already have vectors from a prior --model run.
//
// Either way, chunks_missing_embedding(model_id) gives us the exact
// set, so we never re-embed work that's already on disk.
[[nodiscard]] std::pair<std::size_t, std::int64_t> backfill_embeddings(store::Store& store,
                                                                       embed::Embedder& embedder,
                                                                       bool quiet) {
    const auto missing = store.chunks_missing_embedding(embedder.model_id());
    if (missing.empty()) {
        return {0, 0};
    }

    fmt::print(stderr,
               "\nembedding {} chunk{} with {}...\n",
               missing.size(),
               missing.size() == 1 ? "" : "s",
               embedder.model_id());

    // Batch size matters: too small wastes the model-launch overhead,
    // too large blows tensor memory on bigger models. 32 is a safe
    // middle ground for the Qwen3-Embedding family. Larger inputs are
    // bottlenecked by tokenization anyway.
    constexpr std::size_t kBatchSize = 32;
    std::size_t embedded = 0;
    const auto t0 = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < missing.size(); i += kBatchSize) {
        const std::size_t end = std::min(i + kBatchSize, missing.size());

        // Fetch chunk texts in batch order. We hold the std::strings
        // alive in `texts` so the string_views passed to embed_documents
        // remain valid for the duration of the call.
        std::vector<std::string> texts;
        std::vector<std::string_view> views;
        std::vector<std::string> hashes;
        texts.reserve(end - i);
        views.reserve(end - i);
        hashes.reserve(end - i);
        for (std::size_t j = i; j < end; ++j) {
            auto chunk = store.get_chunk(missing[j]);
            if (!chunk) {
                // Lost a race with a delete? Skip and keep going.
                continue;
            }
            texts.push_back(std::move(chunk->text));
            hashes.push_back(missing[j]);
        }
        for (const auto& t : texts) {
            views.emplace_back(t);
        }

        const auto vectors = embedder.embed_documents(views);
        if (vectors.size() != hashes.size()) {
            throw std::runtime_error(fmt::format(
                "embedder returned {} vectors for {} inputs", vectors.size(), hashes.size()));
        }

        // Stage the batch as parallel views into our owning storage,
        // then upsert with one transaction. Doing this per-row would
        // cost an fsync per chunk — minutes of cumulative I/O on a
        // 10k-chunk repo.
        std::vector<store::Store::EmbeddingPut> puts;
        puts.reserve(vectors.size());
        for (std::size_t j = 0; j < vectors.size(); ++j) {
            puts.push_back({hashes[j], vectors[j]});
        }
        store.put_embeddings(embedder.model_id(), puts);
        embedded += puts.size();

        if (!quiet) {
            fmt::print(stderr, "  embedded {}/{}\n", embedded, missing.size());
        }
    }

    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    return {embedded, dur.count()};
}
#endif  // VECTRA_HAS_EMBED

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
    fmt::print(stderr, "database:  {}\n", db_path.string());

#if VECTRA_HAS_EMBED
    // Load the embedder up front (before walking files) so a missing
    // model file fails fast instead of after we've already chewed
    // through the codebase. nullptr = symbol-only indexing.
    std::unique_ptr<embed::Embedder> embedder;
    try {
        embedder = open_embedder(opts.model);
    } catch (const std::exception& e) {
        fmt::print(stderr, "error: {}\n", e.what());
        return 2;
    }
    if (embedder) {
        fmt::print(stderr, "embedder:  {} (dim {})\n", embedder->model_id(), embedder->dim());
    } else {
        fmt::print(stderr, "embedder:  (none — symbol-only index)\n");
    }
#else
    if (!opts.model.empty()) {
        fmt::print(stderr,
                   "error: this build was produced with VECTRA_BUILD_EMBED=OFF; "
                   "the --model flag is unavailable.\n");
        return 2;
    }
#endif
    fmt::print(stderr, "\n");

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

#if VECTRA_HAS_EMBED
    // Backfill any chunks that lack a vector for the active model.
    // Runs after the file-walk pass — this single call covers both
    // "first-time embedding on a previously symbol-only index" and
    // "incrementally embed only the new chunks from this run."
    if (embedder) {
        try {
            const auto [count, ms] = backfill_embeddings(store, *embedder, opts.quiet);
            stats.chunks_embedded = count;
            stats.embed_time_ms = ms;
        } catch (const std::exception& e) {
            fmt::print(stderr, "\nerror: embedding pass failed: {}\n", e.what());
            // Files were chunked successfully; return non-zero so the
            // caller knows the index is now in a half-embedded state
            // (chunks present, some vectors missing).
            print_summary(stats,
                          std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - t0));
            return 1;
        }
    }
#endif

    const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0);
    print_summary(stats, duration);

    return stats.files_errored == 0 ? 0 : 1;
}

}  // namespace vectra::cli
