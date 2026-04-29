// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra search "<query>"` — runs a hybrid retrieval pass against
// the local index and prints the top-K matches.
//
// Internal to vectra-cli.

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace vectra::cli {

struct SearchOptions {
    // Free-text query. Required.
    std::string query;

    // Database file to query. Defaults to the same convention as
    // `vectra index`: the .vectra/index.db inside the directory we
    // were started in.
    std::filesystem::path db;

    // Number of hits to print.
    std::size_t k = 10;

    // Built-in registry name of the embedding model to use for the
    // vector-side of the hybrid search. Empty means "symbol-only" —
    // useful when no model has been pulled yet, and for quick
    // exact-identifier lookups.
    std::string model;

    // Built-in registry name of a cross-encoder reranker (e.g.
    // "qwen3-rerank-0.6b"). When set, the retriever applies the
    // reranker to the post-fusion candidate pool before truncating
    // to k. Empty means "skip reranking" — fusion score is final.
    std::string reranker;

    // Print full chunk text under each hit. Off by default; the
    // default output is one line per hit so a long result list stays
    // scannable.
    bool show_text = false;
};

// Run the search command. Returns a process exit code (0 on success).
[[nodiscard]] int run_search(const SearchOptions& opts);

}  // namespace vectra::cli
