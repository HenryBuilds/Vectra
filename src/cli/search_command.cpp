// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "search_command.hpp"

#include <fmt/format.h>

#include <filesystem>
#include <memory>
#include <stdexcept>

#include "vectra/core/chunk.hpp"
#include "vectra/retrieve/retriever.hpp"
#include "vectra/store/store.hpp"

#if VECTRA_HAS_EMBED
#include "vectra/embed/embedder.hpp"
#include "vectra/embed/model_registry.hpp"
#endif

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] fs::path resolve_db(const fs::path& explicit_db) {
    if (!explicit_db.empty()) {
        return explicit_db;
    }
    // Walk up from cwd looking for a .vectra/index.db. This mirrors
    // the way `vectra index` writes to <root>/.vectra/index.db.
    auto cur = fs::current_path();
    for (int i = 0; i < 16; ++i) {
        const auto candidate = cur / ".vectra" / "index.db";
        if (fs::exists(candidate)) {
            return candidate;
        }
        const auto parent = cur.parent_path();
        if (parent == cur) {
            break;
        }
        cur = parent;
    }
    throw std::runtime_error(
        "could not locate a Vectra index. Pass --db <path>, or run "
        "`vectra index` first to create one.");
}

void print_hit(std::size_t rank, const retrieve::Hit& hit, bool show_text) {
    const auto kind_name = core::chunk_kind_name(hit.kind);
    const std::string symbol = hit.symbol.empty() ? "(anonymous)" : hit.symbol;

    fmt::print("{:>3}. {:<10}  {:<22}  {}:{}-{}  (score {:.3f})\n",
               rank,
               kind_name,
               symbol,
               hit.file_path,
               hit.start_row + 1,
               hit.end_row + 1,
               hit.score);

    if (show_text) {
        fmt::print("\n{}\n\n", hit.text);
    }
}

}  // namespace

int run_search(const SearchOptions& opts) {
    if (opts.query.empty()) {
        fmt::print(stderr, "error: query is empty\n");
        return 2;
    }

    const auto db_path = resolve_db(opts.db);
    auto store = store::Store::open(db_path);
    fmt::print(stderr, "index: {}\n", db_path.string());

    retrieve::Retriever retriever(store);

#if VECTRA_HAS_EMBED
    // Optional embedder for the vector side of the hybrid search. If
    // the user did not pass --model, we skip the embedding model load
    // and fall back to symbol-only retrieval; the Retriever handles
    // both modes from the same code path.
    //
    // unique_ptr (rather than std::optional) is required because
    // Embedder explicitly deletes move-assignment — it holds a
    // llama_context* whose lifetime cannot be rebound. Heap
    // allocation cost is negligible compared to the model load.
    std::unique_ptr<embed::Embedder> embedder;
    if (!opts.model.empty()) {
        const auto* entry = embed::ModelRegistry::by_name(opts.model);
        if (entry == nullptr) {
            fmt::print(stderr, "error: unknown model '{}'. Try `vectra model list`.\n", opts.model);
            return 2;
        }
        const auto model_path = embed::ModelRegistry::local_path(*entry);
        if (!fs::exists(model_path)) {
            fmt::print(
                stderr, "error: model not cached. Run `vectra model pull {}` first.\n", opts.model);
            return 2;
        }
        embed::EmbedderConfig cfg;
        cfg.model_path = model_path;
        cfg.model_id = entry->name;
        embedder = std::make_unique<embed::Embedder>(embed::Embedder::open(cfg));
        retriever.set_embedder(embedder.get());
        fmt::print(stderr, "model: {} (dim {})\n", entry->name, embedder->dim());
    } else {
        fmt::print(stderr, "model: (none — symbol-only retrieval)\n");
    }
#else
    if (!opts.model.empty()) {
        fmt::print(stderr,
                   "error: this build was produced with VECTRA_BUILD_EMBED=OFF; "
                   "the --model flag is unavailable.\n");
        return 2;
    }
    fmt::print(stderr, "model: (build without embed support — symbol-only)\n");
#endif

    retrieve::RetrieveOptions r_opts;
    r_opts.k = opts.k;

    const auto hits = retriever.retrieve(opts.query, r_opts);

    if (hits.empty()) {
        fmt::print(stderr, "\nno matches.\n");
        return 0;
    }

    fmt::print(stderr, "\n");
    for (std::size_t i = 0; i < hits.size(); ++i) {
        print_hit(i + 1, hits[i], opts.show_text);
    }
    return 0;
}

}  // namespace vectra::cli
