// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Persistence layer for indexed chunks, their embeddings, file
// metadata, and the symbol search index. Backed by SQLite for
// durability and structured queries, and by usearch (HNSW, in
// memory) for approximate-nearest-neighbor vector search.
//
// The Store is the source of truth. The HNSW index is rebuilt from
// SQLite on open() so a corrupted or out-of-date index file is never
// load-bearing.
//
// Threading: a single Store instance is safe to share across threads;
// SQLite uses its serialized threading mode and the in-memory vector
// index is guarded internally.

#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/core/chunk.hpp"

namespace vectra::store {

// One file's metadata. file_blake3 is the hash of the file's full
// content, used as the top-level Merkle marker — when a re-index pass
// sees the same hash, it can skip the file entirely.
struct FileRecord {
    std::string path;
    std::string file_blake3;
    int64_t     last_indexed_at = 0;
};

// One row from a symbol search.
struct SymbolHit {
    std::string       chunk_hash;
    std::string       symbol;
    core::ChunkKind   kind = core::ChunkKind::Unknown;
    double            score = 0.0;     // FTS5 bm25 score, lower is better
};

// One row from an approximate-nearest-neighbor vector search.
struct VectorHit {
    std::string chunk_hash;
    float       distance = 0.0F;       // 1 - cosine similarity, lower is closer
};

class Store {
public:
    // Open or create a Vectra database at `db_path`. If the file does
    // not exist it is created with the current schema; if it exists
    // the schema version is checked and refused if newer than what
    // this binary understands. Throws std::runtime_error on any I/O
    // or schema mismatch.
    [[nodiscard]] static Store open(const std::filesystem::path& db_path);

    ~Store();
    Store(Store&&) noexcept;
    Store& operator=(Store&&) noexcept;
    Store(const Store&)            = delete;
    Store& operator=(const Store&) = delete;

    // ---- Chunks --------------------------------------------------------

    // Insert a chunk against a source file. The chunk's content hash
    // is the primary key; an existing row with the same hash is left
    // untouched (chunks are immutable by content). Symbol-search
    // rows are kept in sync.
    //
    // file_path is taken as a separate argument because Chunk values
    // come from the language-agnostic Chunker which does not see
    // paths — only the caller wiring vectra-core to vectra-store
    // knows where the source came from.
    void put_chunk(std::string_view file_path, const core::Chunk& chunk);

    // Bulk variant. Wraps the inserts in a single transaction.
    void put_chunks(std::string_view file_path, std::span<const core::Chunk> chunks);

    // Lookup by content hash. Returns nullopt if no such chunk exists.
    [[nodiscard]] std::optional<core::Chunk> get_chunk(std::string_view hash) const;

    // All chunks currently associated with a given source-file path.
    [[nodiscard]] std::vector<core::Chunk> chunks_for_file(std::string_view file_path) const;

    // Forget every chunk that was extracted from `file_path`. Linked
    // embeddings and FTS rows are deleted in the same transaction.
    void delete_chunks_for_file(std::string_view file_path);

    // Total number of chunks recorded.
    [[nodiscard]] std::size_t chunk_count() const;

    // ---- Embeddings ----------------------------------------------------

    // Persist the embedding for `chunk_hash` produced by `model_id`.
    // The embedding is also inserted into the in-memory vector index
    // so future search_vectors() calls see it immediately.
    //
    // Replacing an existing embedding (same chunk, same model) is
    // allowed and updates both stores atomically.
    void put_embedding(std::string_view       chunk_hash,
                       std::string_view       model_id,
                       std::span<const float> vector);

    // Retrieve a stored embedding. Returns nullopt if none exists.
    [[nodiscard]] std::optional<std::vector<float>> get_embedding(std::string_view chunk_hash) const;

    // Hashes of chunks that exist in the chunks table but have no
    // embedding row for `model_id`. Useful for batched re-embedding.
    [[nodiscard]] std::vector<std::string> chunks_missing_embedding(std::string_view model_id) const;

    [[nodiscard]] std::size_t embedding_count() const;

    // ---- Vector search -------------------------------------------------

    // Approximate-nearest-neighbor search over all stored embeddings.
    // Returns at most `k` hits, ordered by ascending distance
    // (closest first). The query vector must match the dimension of
    // the model used during indexing.
    [[nodiscard]] std::vector<VectorHit> search_vectors(std::span<const float> query,
                                                        std::size_t            k) const;

    // ---- File metadata -------------------------------------------------

    void put_file_record(const FileRecord& record);

    [[nodiscard]] std::optional<FileRecord> get_file_record(std::string_view path) const;

    // ---- Symbol search -------------------------------------------------

    // FTS5 trigram-tokenized search over chunk symbol names. `limit`
    // caps the number of returned rows. The query is matched
    // case-insensitively and supports tokens, prefixes, and partial
    // matches via FTS5 trigram tokenization.
    [[nodiscard]] std::vector<SymbolHit> search_symbols(std::string_view query,
                                                        std::size_t      limit = 50) const;

    // ---- Schema --------------------------------------------------------

    // Current schema version of the database file.
    [[nodiscard]] int schema_version() const;

    // Pimpl is exposed by name so internal helpers in store.cpp can
    // operate on it without being declared as friends. The struct
    // itself is only ever defined inside the cpp; the alias here is
    // an opaque handle from the consumer's perspective.
    struct Impl;

private:
    Store(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vectra::store
