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

    // Suppress per-file output; print only the final summary.
    bool quiet = false;
};

// Run the index command. Returns a process exit code (0 on success,
// non-zero on user error or unrecoverable failure).
[[nodiscard]] int run_index(const IndexOptions& opts);

}  // namespace vectra::cli
