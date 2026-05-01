// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// `vectra index <path>` — first end-to-end command. Walks a source
// tree, parses each file via vectra-core, and persists chunks +
// per-file Merkle records into vectra-store.
//
// Internal to vectra-cli.

#pragma once

#include <filesystem>

namespace vectra::cli {

struct IndexOptions {
    // Directory whose source files should be indexed. Required.
    std::filesystem::path root;

    // Database file. Defaults to <root>/.vectra/index.db when empty.
    std::filesystem::path db;

    // Directory containing languages.toml and queries/. When empty,
    // resolved by walking up from the current working directory.
    std::filesystem::path resources;

    // Embedding-model registry name. Empty means "skip embedding —
    // build a symbol-only index". When set, the indexer loads the
    // model after the chunking pass and embeds every chunk that
    // does not already have a vector for this model_id, persisting
    // them via Store::put_embedding. This is the path that turns a
    // freshly-walked index into a hybrid (FTS5 + vector) one.
    //
    // Mixing two embedding models in a single index is not
    // supported; if you swap models, delete .vectra/index.db and
    // re-index from scratch.
    std::string model;

    // Suppress per-file output; print only the final summary.
    bool quiet = false;
};

// Run the index command. Returns a process exit code (0 on success,
// non-zero on user error or unrecoverable failure).
[[nodiscard]] int run_index(const IndexOptions& opts);

}  // namespace vectra::cli
