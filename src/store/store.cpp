// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/store/store.hpp"

#include <fmt/format.h>

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <utility>

#include "schema.hpp"
#include "sqlite_helpers.hpp"
#include "vector_index.hpp"

namespace vectra::store {

namespace {

[[nodiscard]] int64_t now_seconds() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Each float is 4 bytes on every platform we target, but we treat the
// blob format explicitly to make the on-disk format independent of
// the host's float layout. (IEEE-754 little-endian; matches every
// consumer machine we ship to.)
[[nodiscard]] std::vector<float> bytes_to_floats(std::span<const std::byte> bytes) {
    if (bytes.size() % sizeof(float) != 0) {
        throw std::runtime_error(fmt::format(
            "stored embedding blob length {} is not a multiple of sizeof(float)", bytes.size()));
    }
    std::vector<float> out(bytes.size() / sizeof(float));
    std::memcpy(out.data(), bytes.data(), bytes.size());
    return out;
}

[[nodiscard]] std::span<const std::byte> floats_to_bytes(std::span<const float> v) {
    return {reinterpret_cast<const std::byte*>(v.data()), v.size() * sizeof(float)};
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Store::Impl {
    detail::SqliteHandle db;
    std::unique_ptr<detail::VectorIndex> vectors;  // lazily created on first embedding

    // Pre-prepared statements. We pay the parse cost once on open()
    // and reuse them for the lifetime of the Store.
    detail::StmtHandle stmt_chunk_insert;
    detail::StmtHandle stmt_chunk_get_by_hash;
    detail::StmtHandle stmt_chunks_by_file;
    detail::StmtHandle stmt_chunk_ids_by_file;
    detail::StmtHandle stmt_chunks_delete_by_file;
    detail::StmtHandle stmt_chunk_count;
    detail::StmtHandle stmt_chunk_id_by_hash;
    detail::StmtHandle stmt_chunk_hash_by_id;

    detail::StmtHandle stmt_embedding_upsert;
    detail::StmtHandle stmt_embedding_get;
    detail::StmtHandle stmt_embedding_count;
    detail::StmtHandle stmt_embedding_missing;
    detail::StmtHandle stmt_embedding_dim_probe;
    detail::StmtHandle stmt_embedding_load_all;

    detail::StmtHandle stmt_file_upsert;
    detail::StmtHandle stmt_file_get;

    detail::StmtHandle stmt_symbol_insert;
    detail::StmtHandle stmt_symbol_delete_by_hash;
    detail::StmtHandle stmt_symbol_search;
};

namespace {

void prepare_all(Store::Impl& impl) {
    auto* db = impl.db.get();
    impl.stmt_chunk_insert = detail::prepare(db, R"SQL(
        INSERT OR IGNORE INTO chunks
            (hash, file_path, language, kind, symbol,
             start_byte, end_byte, start_row, end_row, text,
             created_at, updated_at)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )SQL");

    impl.stmt_chunk_get_by_hash = detail::prepare(db, R"SQL(
        SELECT hash, file_path, language, kind, symbol,
               start_byte, end_byte, start_row, end_row, text
          FROM chunks WHERE hash = ?
    )SQL");

    impl.stmt_chunks_by_file = detail::prepare(db, R"SQL(
        SELECT hash, file_path, language, kind, symbol,
               start_byte, end_byte, start_row, end_row, text
          FROM chunks WHERE file_path = ?
         ORDER BY start_byte
    )SQL");

    impl.stmt_chunk_ids_by_file = detail::prepare(db, R"SQL(
        SELECT id FROM chunks WHERE file_path = ?
    )SQL");

    impl.stmt_chunks_delete_by_file = detail::prepare(db, R"SQL(
        DELETE FROM chunks WHERE file_path = ?
    )SQL");

    impl.stmt_chunk_count = detail::prepare(db, "SELECT COUNT(*) FROM chunks");

    impl.stmt_chunk_id_by_hash = detail::prepare(db, R"SQL(
        SELECT id FROM chunks WHERE hash = ?
    )SQL");

    impl.stmt_chunk_hash_by_id = detail::prepare(db, R"SQL(
        SELECT hash FROM chunks WHERE id = ?
    )SQL");

    impl.stmt_embedding_upsert = detail::prepare(db, R"SQL(
        INSERT OR REPLACE INTO embeddings
            (chunk_id, model_id, embed_dim, vector, embedded_at)
        VALUES (?, ?, ?, ?, ?)
    )SQL");

    impl.stmt_embedding_get = detail::prepare(db, R"SQL(
        SELECT vector FROM embeddings
         WHERE chunk_id = (SELECT id FROM chunks WHERE hash = ?)
         LIMIT 1
    )SQL");

    impl.stmt_embedding_count = detail::prepare(db, "SELECT COUNT(*) FROM embeddings");

    impl.stmt_embedding_missing = detail::prepare(db, R"SQL(
        SELECT c.hash
          FROM chunks c
         WHERE NOT EXISTS (
             SELECT 1 FROM embeddings e
              WHERE e.chunk_id = c.id AND e.model_id = ?
         )
    )SQL");

    impl.stmt_embedding_dim_probe = detail::prepare(db, R"SQL(
        SELECT embed_dim FROM embeddings LIMIT 1
    )SQL");

    impl.stmt_embedding_load_all = detail::prepare(db, R"SQL(
        SELECT chunk_id, vector FROM embeddings
    )SQL");

    impl.stmt_file_upsert = detail::prepare(db, R"SQL(
        INSERT INTO files (path, file_blake3, last_indexed_at)
        VALUES (?, ?, ?)
        ON CONFLICT(path) DO UPDATE SET
            file_blake3 = excluded.file_blake3,
            last_indexed_at = excluded.last_indexed_at
    )SQL");

    impl.stmt_file_get = detail::prepare(db, R"SQL(
        SELECT path, file_blake3, last_indexed_at FROM files WHERE path = ?
    )SQL");

    impl.stmt_symbol_insert = detail::prepare(db, R"SQL(
        INSERT INTO symbols_fts (symbol, chunk_hash, kind) VALUES (?, ?, ?)
    )SQL");

    impl.stmt_symbol_delete_by_hash = detail::prepare(db, R"SQL(
        DELETE FROM symbols_fts WHERE chunk_hash = ?
    )SQL");

    impl.stmt_symbol_search = detail::prepare(db, R"SQL(
        SELECT chunk_hash, symbol, kind, bm25(symbols_fts) AS score
          FROM symbols_fts
         WHERE symbols_fts MATCH ?
         ORDER BY score
         LIMIT ?
    )SQL");
}

void rebuild_vector_index(Store::Impl& impl) {
    auto* db = impl.db.get();

    // If there are no embeddings yet, leave vectors null. We will
    // create the index lazily when the first put_embedding arrives.
    int dim = 0;
    {
        detail::ResetGuard guard(impl.stmt_embedding_dim_probe.get());
        const int rc = sqlite3_step(impl.stmt_embedding_dim_probe.get());
        if (rc == SQLITE_ROW) {
            dim = detail::column_int(impl.stmt_embedding_dim_probe.get(), 0);
        } else if (rc != SQLITE_DONE) {
            detail::check(db, rc, "probe embedding dim");
        }
    }
    if (dim == 0)
        return;

    // Count rows up front so we can reserve exact capacity instead of
    // growing the HNSW graph during the rebuild loop. A separate
    // statement keeps this self-contained.
    std::size_t n_rows = 0;
    {
        detail::StmtHandle count_stmt = detail::prepare(db, "SELECT COUNT(*) FROM embeddings");
        const int rc = sqlite3_step(count_stmt.get());
        detail::check(db, rc, "count embeddings");
        n_rows = static_cast<std::size_t>(detail::column_int64(count_stmt.get(), 0));
    }

    impl.vectors = std::make_unique<detail::VectorIndex>(static_cast<std::uint32_t>(dim));
    impl.vectors->reserve(n_rows > 0 ? n_rows : std::size_t{1024});

    detail::ResetGuard guard(impl.stmt_embedding_load_all.get());
    while (true) {
        const int rc = sqlite3_step(impl.stmt_embedding_load_all.get());
        if (rc == SQLITE_DONE)
            break;
        detail::check(db, rc, "iterate embeddings");

        const auto chunk_id =
            static_cast<std::uint64_t>(detail::column_int64(impl.stmt_embedding_load_all.get(), 0));
        const auto blob = detail::column_blob(impl.stmt_embedding_load_all.get(), 1);
        const auto floats = bytes_to_floats(blob);
        impl.vectors->upsert(chunk_id, floats);
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction / lifecycle
// ---------------------------------------------------------------------------

Store::Store(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Store::~Store() = default;
Store::Store(Store&&) noexcept = default;
Store& Store::operator=(Store&&) noexcept = default;

Store Store::open(const std::filesystem::path& db_path) {
    auto impl = std::make_unique<Impl>();

    sqlite3* raw = nullptr;
    const int rc =
        sqlite3_open_v2(db_path.string().c_str(),
                        &raw,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                        nullptr);
    impl->db.reset(raw);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(
            fmt::format("sqlite3_open_v2('{}') failed with code {}", db_path.string(), rc));
    }

    detail::ensure_schema(impl->db.get());
    prepare_all(*impl);
    rebuild_vector_index(*impl);

    return Store{std::move(impl)};
}

int Store::schema_version() const {
    auto stmt = detail::prepare(impl_->db.get(), "PRAGMA user_version");
    const int rc = sqlite3_step(stmt.get());
    detail::check(impl_->db.get(), rc, "read user_version");
    return detail::column_int(stmt.get(), 0);
}

// ---------------------------------------------------------------------------
// Chunks
// ---------------------------------------------------------------------------

void Store::put_chunk(std::string_view file_path, const core::Chunk& chunk) {
    put_chunks(file_path, std::span{&chunk, 1});
}

void Store::put_chunks(std::string_view file_path, std::span<const core::Chunk> chunks) {
    auto* db = impl_->db.get();
    detail::in_transaction(db, [&] {
        const int64_t now = now_seconds();

        for (const auto& c : chunks) {
            const std::string hash_hex = c.content_hash.to_hex();

            // Insert the chunk row. INSERT OR IGNORE skips duplicates
            // — the (hash) UNIQUE constraint prevents collisions, and
            // chunks are immutable by content so re-inserting the
            // same hash is a no-op by design.
            {
                auto* s = impl_->stmt_chunk_insert.get();
                detail::ResetGuard guard(s);
                detail::bind_text(s, 1, hash_hex);
                detail::bind_text(s, 2, file_path);
                detail::bind_text(s, 3, c.language);
                detail::bind_int(s, 4, static_cast<int>(c.kind));
                if (c.symbol.empty())
                    detail::bind_null(s, 5);
                else
                    detail::bind_text(s, 5, c.symbol);
                detail::bind_int64(s, 6, c.range.start_byte);
                detail::bind_int64(s, 7, c.range.end_byte);
                detail::bind_int64(s, 8, c.range.start_row);
                detail::bind_int64(s, 9, c.range.end_row);
                detail::bind_text(s, 10, c.text);
                detail::bind_int64(s, 11, now);
                detail::bind_int64(s, 12, now);

                const int rc = sqlite3_step(s);
                detail::check(db, rc, "insert chunk");
            }

            // Mirror the symbol into the FTS index. INSERT OR IGNORE
            // on the chunks table means a duplicate hash produces no
            // new chunk row; we skip the symbol row in that case to
            // avoid duplicate FTS entries.
            if (sqlite3_changes(db) != 0 && !c.symbol.empty()) {
                auto* s = impl_->stmt_symbol_insert.get();
                detail::ResetGuard guard(s);
                detail::bind_text(s, 1, c.symbol);
                detail::bind_text(s, 2, hash_hex);
                detail::bind_int(s, 3, static_cast<int>(c.kind));
                const int rc = sqlite3_step(s);
                detail::check(db, rc, "insert symbol_fts row");
            }
        }
    });
}

std::optional<core::Chunk> Store::get_chunk(std::string_view hash) const {
    auto* s = impl_->stmt_chunk_get_by_hash.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, hash);

    const int rc = sqlite3_step(s);
    if (rc == SQLITE_DONE)
        return std::nullopt;
    detail::check(impl_->db.get(), rc, "get_chunk");

    core::Chunk c;
    c.content_hash = core::Blake3Hash::from_hex(detail::column_text(s, 0));
    c.file_path = detail::column_text(s, 1);
    c.language = detail::column_text(s, 2);
    c.kind = static_cast<core::ChunkKind>(detail::column_int(s, 3));
    c.symbol = detail::column_text(s, 4);
    c.range.start_byte = static_cast<std::uint32_t>(detail::column_int64(s, 5));
    c.range.end_byte = static_cast<std::uint32_t>(detail::column_int64(s, 6));
    c.range.start_row = static_cast<std::uint32_t>(detail::column_int64(s, 7));
    c.range.end_row = static_cast<std::uint32_t>(detail::column_int64(s, 8));
    c.text = detail::column_text(s, 9);
    return c;
}

std::vector<core::Chunk> Store::chunks_for_file(std::string_view file_path) const {
    auto* s = impl_->stmt_chunks_by_file.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, file_path);

    std::vector<core::Chunk> out;
    while (true) {
        const int rc = sqlite3_step(s);
        if (rc == SQLITE_DONE)
            break;
        detail::check(impl_->db.get(), rc, "chunks_for_file");

        core::Chunk c;
        c.content_hash = core::Blake3Hash::from_hex(detail::column_text(s, 0));
        c.file_path = detail::column_text(s, 1);
        c.language = detail::column_text(s, 2);
        c.kind = static_cast<core::ChunkKind>(detail::column_int(s, 3));
        c.symbol = detail::column_text(s, 4);
        c.range.start_byte = static_cast<std::uint32_t>(detail::column_int64(s, 5));
        c.range.end_byte = static_cast<std::uint32_t>(detail::column_int64(s, 6));
        c.range.start_row = static_cast<std::uint32_t>(detail::column_int64(s, 7));
        c.range.end_row = static_cast<std::uint32_t>(detail::column_int64(s, 8));
        c.text = detail::column_text(s, 9);
        out.push_back(std::move(c));
    }
    return out;
}

void Store::delete_chunks_for_file(std::string_view file_path) {
    auto* db = impl_->db.get();

    // Collect the chunk_ids first so we know what to remove from the
    // in-memory vector index after the SQL delete cascades. We do
    // this OUTSIDE the transaction because reads on the same
    // connection inside an IMMEDIATE transaction would conflict; the
    // ids are stable until the delete runs anyway.
    std::vector<std::uint64_t> ids_to_remove;
    {
        auto* s = impl_->stmt_chunk_ids_by_file.get();
        detail::ResetGuard guard(s);
        detail::bind_text(s, 1, file_path);
        while (true) {
            const int rc = sqlite3_step(s);
            if (rc == SQLITE_DONE)
                break;
            detail::check(db, rc, "list chunk ids by file");
            ids_to_remove.push_back(static_cast<std::uint64_t>(detail::column_int64(s, 0)));
        }
    }

    // Collect hashes to clean up in the FTS table.
    std::vector<std::string> hashes_to_remove;
    {
        auto* s = impl_->stmt_chunks_by_file.get();
        detail::ResetGuard guard(s);
        detail::bind_text(s, 1, file_path);
        while (true) {
            const int rc = sqlite3_step(s);
            if (rc == SQLITE_DONE)
                break;
            detail::check(db, rc, "list chunk hashes by file");
            hashes_to_remove.push_back(detail::column_text(s, 0));
        }
    }

    detail::in_transaction(db, [&] {
        // FTS first because it carries no cascade.
        for (const auto& h : hashes_to_remove) {
            auto* s = impl_->stmt_symbol_delete_by_hash.get();
            detail::ResetGuard guard(s);
            detail::bind_text(s, 1, h);
            const int rc = sqlite3_step(s);
            detail::check(db, rc, "delete symbol_fts rows");
        }

        // chunks → embeddings cascade via the FK.
        auto* s = impl_->stmt_chunks_delete_by_file.get();
        detail::ResetGuard guard(s);
        detail::bind_text(s, 1, file_path);
        const int rc = sqlite3_step(s);
        detail::check(db, rc, "delete chunks by file");
    });

    if (impl_->vectors) {
        for (auto id : ids_to_remove) {
            impl_->vectors->remove(id);
        }
    }
}

std::size_t Store::chunk_count() const {
    auto* s = impl_->stmt_chunk_count.get();
    detail::ResetGuard guard(s);
    const int rc = sqlite3_step(s);
    detail::check(impl_->db.get(), rc, "chunk_count");
    return static_cast<std::size_t>(detail::column_int64(s, 0));
}

// ---------------------------------------------------------------------------
// Embeddings
// ---------------------------------------------------------------------------

namespace {

[[nodiscard]] std::int64_t lookup_chunk_id(Store::Impl& impl, std::string_view hash) {
    auto* s = impl.stmt_chunk_id_by_hash.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, hash);
    const int rc = sqlite3_step(s);
    if (rc == SQLITE_DONE) {
        throw std::runtime_error(
            fmt::format("no chunk with hash '{}' — call put_chunk before put_embedding", hash));
    }
    detail::check(impl.db.get(), rc, "lookup chunk id by hash");
    return detail::column_int64(s, 0);
}

}  // namespace

void Store::put_embedding(std::string_view chunk_hash,
                          std::string_view model_id,
                          std::span<const float> vector) {
    if (vector.empty()) {
        throw std::runtime_error("put_embedding: vector is empty");
    }

    if (!impl_->vectors) {
        impl_->vectors =
            std::make_unique<detail::VectorIndex>(static_cast<std::uint32_t>(vector.size()));
        // usearch's add() segfaults against an unreserved index; the
        // initial capacity is a soft hint that the index grows past
        // automatically (we double it below when we approach the
        // limit).
        impl_->vectors->reserve(1024);
    } else if (impl_->vectors->dim() != vector.size()) {
        throw std::runtime_error(
            fmt::format("put_embedding: vector dim {} does not match index dim {}; "
                        "an embedding model swap requires a fresh index",
                        vector.size(),
                        impl_->vectors->dim()));
    }

    const std::int64_t chunk_id = lookup_chunk_id(*impl_, chunk_hash);
    const std::int64_t now = now_seconds();
    const auto bytes = floats_to_bytes(vector);

    detail::in_transaction(impl_->db.get(), [&] {
        auto* s = impl_->stmt_embedding_upsert.get();
        detail::ResetGuard guard(s);
        detail::bind_int64(s, 1, chunk_id);
        detail::bind_text(s, 2, model_id);
        detail::bind_int(s, 3, static_cast<int>(vector.size()));
        detail::bind_blob(s, 4, bytes);
        detail::bind_int64(s, 5, now);
        const int rc = sqlite3_step(s);
        detail::check(impl_->db.get(), rc, "upsert embedding");
    });

    impl_->vectors->upsert(static_cast<std::uint64_t>(chunk_id), vector);
}

std::optional<std::vector<float>> Store::get_embedding(std::string_view chunk_hash) const {
    auto* s = impl_->stmt_embedding_get.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, chunk_hash);

    const int rc = sqlite3_step(s);
    if (rc == SQLITE_DONE)
        return std::nullopt;
    detail::check(impl_->db.get(), rc, "get_embedding");

    return bytes_to_floats(detail::column_blob(s, 0));
}

std::vector<std::string> Store::chunks_missing_embedding(std::string_view model_id) const {
    auto* s = impl_->stmt_embedding_missing.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, model_id);

    std::vector<std::string> out;
    while (true) {
        const int rc = sqlite3_step(s);
        if (rc == SQLITE_DONE)
            break;
        detail::check(impl_->db.get(), rc, "chunks_missing_embedding");
        out.push_back(detail::column_text(s, 0));
    }
    return out;
}

std::size_t Store::embedding_count() const {
    auto* s = impl_->stmt_embedding_count.get();
    detail::ResetGuard guard(s);
    const int rc = sqlite3_step(s);
    detail::check(impl_->db.get(), rc, "embedding_count");
    return static_cast<std::size_t>(detail::column_int64(s, 0));
}

// ---------------------------------------------------------------------------
// Vector search
// ---------------------------------------------------------------------------

std::vector<VectorHit> Store::search_vectors(std::span<const float> query, std::size_t k) const {
    if (!impl_->vectors)
        return {};
    const auto raw = impl_->vectors->search(query, k);

    std::vector<VectorHit> out;
    out.reserve(raw.size());
    for (const auto& r : raw) {
        auto* s = impl_->stmt_chunk_hash_by_id.get();
        detail::ResetGuard guard(s);
        detail::bind_int64(s, 1, static_cast<std::int64_t>(r.key));
        const int rc = sqlite3_step(s);
        if (rc == SQLITE_DONE)
            continue;  // stale id, ignore
        detail::check(impl_->db.get(), rc, "search_vectors hash lookup");

        VectorHit hit;
        hit.chunk_hash = detail::column_text(s, 0);
        hit.distance = r.distance;
        out.push_back(std::move(hit));
    }
    return out;
}

// ---------------------------------------------------------------------------
// File records
// ---------------------------------------------------------------------------

void Store::put_file_record(const FileRecord& record) {
    auto* s = impl_->stmt_file_upsert.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, record.path);
    detail::bind_text(s, 2, record.file_blake3);
    detail::bind_int64(s, 3, record.last_indexed_at);
    const int rc = sqlite3_step(s);
    detail::check(impl_->db.get(), rc, "put_file_record");
}

std::optional<FileRecord> Store::get_file_record(std::string_view path) const {
    auto* s = impl_->stmt_file_get.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, path);

    const int rc = sqlite3_step(s);
    if (rc == SQLITE_DONE)
        return std::nullopt;
    detail::check(impl_->db.get(), rc, "get_file_record");

    FileRecord r;
    r.path = detail::column_text(s, 0);
    r.file_blake3 = detail::column_text(s, 1);
    r.last_indexed_at = detail::column_int64(s, 2);
    return r;
}

// ---------------------------------------------------------------------------
// Symbol search
// ---------------------------------------------------------------------------

namespace {

// Sanitize a free-text query for FTS5 MATCH. FTS5 has its own
// expression language with operators (AND/OR/NOT), prefix wildcards
// (*), phrase quotes ("..."), grouping parentheses, and special
// punctuation (+ - : ?). User prompts in natural language regularly
// contain those — a question mark or an apostrophe trips a parser
// error before any rows are scanned.
//
// Strategy: extract identifier-like runs (alphanumeric + underscore),
// wrap each in double quotes (FTS5's literal-phrase syntax), and OR
// them together. Token-free input returns the empty string, which
// the caller treats as "skip the search" rather than handing FTS5
// an invalid expression.
[[nodiscard]] std::string sanitize_fts5_query(std::string_view raw) {
    std::vector<std::string> tokens;
    std::string cur;
    for (char c : raw) {
        const auto uc = static_cast<unsigned char>(c);
        if (std::isalnum(uc) != 0 || c == '_') {
            cur.push_back(c);
        } else if (!cur.empty()) {
            tokens.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) {
        tokens.push_back(std::move(cur));
    }

    std::string out;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        if (i > 0) {
            out += " OR ";
        }
        out += '"';
        out += tokens[i];
        out += '"';
    }
    return out;
}

}  // namespace

std::vector<SymbolHit> Store::search_symbols(std::string_view query, std::size_t limit) const {
    const std::string match_expr = sanitize_fts5_query(query);
    if (match_expr.empty()) {
        return {};
    }

    auto* s = impl_->stmt_symbol_search.get();
    detail::ResetGuard guard(s);
    detail::bind_text(s, 1, match_expr);
    detail::bind_int64(s, 2, static_cast<std::int64_t>(limit));

    std::vector<SymbolHit> out;
    while (true) {
        const int rc = sqlite3_step(s);
        if (rc == SQLITE_DONE)
            break;
        detail::check(impl_->db.get(), rc, "search_symbols");

        SymbolHit hit;
        hit.chunk_hash = detail::column_text(s, 0);
        hit.symbol = detail::column_text(s, 1);
        hit.kind = static_cast<core::ChunkKind>(detail::column_int(s, 2));
        hit.score = sqlite3_column_double(s, 3);
        out.push_back(std::move(hit));
    }
    return out;
}

}  // namespace vectra::store
