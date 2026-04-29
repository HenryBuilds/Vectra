// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "schema.hpp"

#include <fmt/format.h>

#include <stdexcept>

#include "sqlite_helpers.hpp"

namespace vectra::store::detail {

namespace {

// Pragmas applied to every connection. WAL gives us readers that
// don't block writers; the rest are sane defaults for an interactive
// indexer.
constexpr const char* kPragmas = R"SQL(
    PRAGMA journal_mode = WAL;
    PRAGMA synchronous  = NORMAL;
    PRAGMA foreign_keys = ON;
    PRAGMA temp_store   = MEMORY;
)SQL";

// Schema v1: the minimum surface to round-trip chunks, embeddings,
// per-file metadata, and a trigram-tokenized symbol search index.
//
// Notes on the design:
//
//   - chunks.id is a stable integer used as the usearch label so the
//     vector index can map back to a chunk without storing strings.
//     `hash` remains the user-visible primary identifier.
//
//   - embeddings.model_id and embed_dim let us notice mid-flight
//     model changes (different dim → reindex required) and let
//     multiple embedding models coexist for experimentation.
//
//   - symbols_fts uses the trigram tokenizer because it is the right
//     primitive for code-identifier search: case-insensitive,
//     substring-friendly, no language-specific stemming.
constexpr const char* kSchemaV1 = R"SQL(
    CREATE TABLE IF NOT EXISTS chunks (
        id           INTEGER PRIMARY KEY AUTOINCREMENT,
        hash         TEXT    NOT NULL UNIQUE,
        file_path    TEXT    NOT NULL,
        language     TEXT    NOT NULL,
        kind         INTEGER NOT NULL,
        symbol       TEXT,
        start_byte   INTEGER NOT NULL,
        end_byte     INTEGER NOT NULL,
        start_row    INTEGER NOT NULL,
        end_row      INTEGER NOT NULL,
        text         TEXT    NOT NULL,
        created_at   INTEGER NOT NULL,
        updated_at   INTEGER NOT NULL
    );

    CREATE INDEX IF NOT EXISTS idx_chunks_file_path ON chunks(file_path);

    CREATE TABLE IF NOT EXISTS embeddings (
        chunk_id    INTEGER NOT NULL,
        model_id    TEXT    NOT NULL,
        embed_dim   INTEGER NOT NULL,
        vector      BLOB    NOT NULL,
        embedded_at INTEGER NOT NULL,
        PRIMARY KEY (chunk_id, model_id),
        FOREIGN KEY (chunk_id) REFERENCES chunks(id) ON DELETE CASCADE
    );

    CREATE TABLE IF NOT EXISTS files (
        path             TEXT    PRIMARY KEY,
        file_blake3      TEXT    NOT NULL,
        last_indexed_at  INTEGER NOT NULL
    );

    CREATE VIRTUAL TABLE IF NOT EXISTS symbols_fts USING fts5(
        symbol,
        chunk_hash UNINDEXED,
        kind       UNINDEXED,
        tokenize   = 'trigram'
    );
)SQL";

[[nodiscard]] int read_user_version(sqlite3* db) {
    StmtHandle stmt = prepare(db, "PRAGMA user_version");
    const int rc = sqlite3_step(stmt.get());
    check(db, rc, "read PRAGMA user_version");
    return column_int(stmt.get(), 0);
}

void write_user_version(sqlite3* db, int version) {
    exec(db, fmt::format("PRAGMA user_version = {}", version));
}

}  // namespace

void ensure_schema(sqlite3* db) {
    exec(db, kPragmas);

    const int existing = read_user_version(db);
    if (existing > kSchemaVersion) {
        throw std::runtime_error(
            fmt::format("vectra: database schema version is {}, but this binary "
                        "only understands up to version {}. Use a newer Vectra build "
                        "or start from a fresh index.",
                        existing,
                        kSchemaVersion));
    }

    // user_version 0 means a fresh database. Any older non-zero
    // version means we'd need a migration path; until we introduce
    // multiple schema versions, the only legal states are 0 (apply
    // kSchemaV1) and kSchemaVersion (no-op).
    if (existing == 0) {
        in_transaction(db, [&] {
            exec(db, kSchemaV1);
            write_user_version(db, kSchemaVersion);
        });
    } else if (existing != kSchemaVersion) {
        throw std::runtime_error(
            fmt::format("vectra: database is at schema version {} but no migration "
                        "path to {} is implemented yet.",
                        existing,
                        kSchemaVersion));
    }
}

}  // namespace vectra::store::detail
