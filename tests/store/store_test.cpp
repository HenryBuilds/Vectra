// Copyright 2026 Vectra Contributors. Apache-2.0.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <system_error>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "vectra/core/chunk.hpp"
#include "vectra/core/hash.hpp"
#include "vectra/store/store.hpp"

using vectra::core::Blake3Hash;
using vectra::core::Chunk;
using vectra::core::ChunkKind;
using vectra::core::hash_string;
using vectra::core::Range;
using vectra::store::FileRecord;
using vectra::store::Store;

namespace {

// Build a fresh, unique SQLite path for each test invocation.
//
// The path is namespaced by both a per-process session id (the
// monotonic clock at process start) and a per-test counter so two
// concurrent test runs cannot collide, and so leftover .db / .db-wal
// files from previous runs are never reopened by accident — that
// silently surfaced as cascading SIGSEGV's the first time the store
// suite ran on a system where the previous build had a partially
// applied schema.
//
// The file (and its WAL/SHM siblings) is removed before the path is
// returned so each test starts from a clean slate.
std::filesystem::path tmp_db_path() {
    static const auto session_id = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};

    auto base = std::filesystem::temp_directory_path() / "vectra-test";
    std::filesystem::create_directories(base);

    auto p = base / ("store-" + session_id + "-" +
                     std::to_string(counter.fetch_add(1)) + ".db");
    std::error_code ec;
    std::filesystem::remove(p,                                ec);
    std::filesystem::remove(p.string() + "-wal",              ec);
    std::filesystem::remove(p.string() + "-shm",              ec);
    std::filesystem::remove(p.string() + "-journal",          ec);
    return p;
}

Chunk make_chunk(std::string symbol, std::string text, ChunkKind kind = ChunkKind::Function) {
    Chunk c;
    c.language     = "python";
    c.kind         = kind;
    c.symbol       = std::move(symbol);
    c.range        = Range{0, static_cast<uint32_t>(text.size()), 0, 0};
    c.content_hash = hash_string(text);
    c.text         = std::move(text);
    return c;
}

}  // namespace

TEST_CASE("Store: open creates schema and reports current version", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);
    REQUIRE(store.schema_version() == 1);
    REQUIRE(store.chunk_count() == 0);
    REQUIRE(store.embedding_count() == 0);
}

TEST_CASE("Store: chunk round-trip via put/get_chunk", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    auto c = make_chunk("greet", "def greet(name): return f'Hello, {name}'");
    store.put_chunk("greet.py", c);

    REQUIRE(store.chunk_count() == 1);

    const auto fetched = store.get_chunk(c.content_hash.to_hex());
    REQUIRE(fetched.has_value());
    REQUIRE(fetched->symbol == "greet");
    REQUIRE(fetched->text == c.text);
    REQUIRE(fetched->kind == ChunkKind::Function);
    REQUIRE(fetched->content_hash == c.content_hash);
}

TEST_CASE("Store: chunks_for_file groups by file", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    auto a = make_chunk("a", "def a(): pass");
    auto b = make_chunk("b", "def b(): pass");
    auto c = make_chunk("c", "def c(): pass");

    store.put_chunk("first.py", a);
    store.put_chunk("first.py", b);
    store.put_chunk("second.py", c);

    REQUIRE(store.chunks_for_file("first.py").size() == 2);
    REQUIRE(store.chunks_for_file("second.py").size() == 1);
    REQUIRE(store.chunks_for_file("missing.py").empty());
}

TEST_CASE("Store: delete_chunks_for_file cascades to symbols", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    auto a = make_chunk("alpha", "def alpha(): pass");
    auto b = make_chunk("beta",  "def beta(): pass");

    store.put_chunk("file.py", a);
    store.put_chunk("file.py", b);
    REQUIRE(store.chunk_count() == 2);

    // FTS lookup before delete.
    REQUIRE_FALSE(store.search_symbols("alpha").empty());

    store.delete_chunks_for_file("file.py");
    REQUIRE(store.chunk_count() == 0);
    REQUIRE(store.chunks_for_file("file.py").empty());

    // Symbol search must no longer return the deleted symbols.
    REQUIRE(store.search_symbols("alpha").empty());
    REQUIRE(store.search_symbols("beta").empty());
}

TEST_CASE("Store: search_symbols matches via trigram prefix", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    store.put_chunk("a.py", make_chunk("getUser",      "def getUser(): pass"));
    store.put_chunk("b.py", make_chunk("getUserByID",  "def getUserByID(): pass"));
    store.put_chunk("c.py", make_chunk("setPassword",  "def setPassword(): pass"));

    auto hits = store.search_symbols("getUser");
    REQUIRE(hits.size() == 2);
    const bool has_user_by_id = std::any_of(
        hits.begin(), hits.end(),
        [](const auto& h) { return h.symbol == "getUserByID"; });
    REQUIRE(has_user_by_id);

    REQUIRE(store.search_symbols("setPassword").size() == 1);
    REQUIRE(store.search_symbols("nonexistent").empty());
}

TEST_CASE("Store: file records round-trip", "[store]") {
    auto path  = tmp_db_path();
    auto store = Store::open(path);

    FileRecord r{"src/main.py", "deadbeef", 1700000000};
    store.put_file_record(r);

    auto got = store.get_file_record("src/main.py");
    REQUIRE(got.has_value());
    REQUIRE(got->path == "src/main.py");
    REQUIRE(got->file_blake3 == "deadbeef");
    REQUIRE(got->last_indexed_at == 1700000000);

    // Update path replaces the stored hash.
    store.put_file_record({"src/main.py", "cafebabe", 1700001000});
    got = store.get_file_record("src/main.py");
    REQUIRE(got.has_value());
    REQUIRE(got->file_blake3 == "cafebabe");
    REQUIRE(got->last_indexed_at == 1700001000);
}

TEST_CASE("Store: embedding round-trip and vector search", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    auto c1 = make_chunk("alpha", "def alpha(): pass");
    auto c2 = make_chunk("beta",  "def beta(): pass");
    store.put_chunk("f.py", c1);
    store.put_chunk("f.py", c2);

    // Two simple 4-d vectors. Search with vec1 should rank c1 first.
    const std::vector<float> vec1 = {1.0F, 0.0F, 0.0F, 0.0F};
    const std::vector<float> vec2 = {0.0F, 1.0F, 0.0F, 0.0F};
    store.put_embedding(c1.content_hash.to_hex(), "qwen3-0.6b", vec1);
    store.put_embedding(c2.content_hash.to_hex(), "qwen3-0.6b", vec2);

    REQUIRE(store.embedding_count() == 2);

    auto fetched = store.get_embedding(c1.content_hash.to_hex());
    REQUIRE(fetched.has_value());
    REQUIRE(fetched->size() == 4);
    REQUIRE((*fetched)[0] == 1.0F);

    const auto hits = store.search_vectors(vec1, 2);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].chunk_hash == c1.content_hash.to_hex());
    REQUIRE(hits[0].distance <= hits[1].distance);
}

TEST_CASE("Store: chunks_missing_embedding lists chunks without an embedding", "[store]") {
    auto path = tmp_db_path();
    auto store = Store::open(path);

    auto a = make_chunk("a", "def a(): pass");
    auto b = make_chunk("b", "def b(): pass");
    store.put_chunk("f.py", a);
    store.put_chunk("f.py", b);

    auto missing = store.chunks_missing_embedding("qwen3-0.6b");
    REQUIRE(missing.size() == 2);

    store.put_embedding(a.content_hash.to_hex(), "qwen3-0.6b",
                        std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});

    missing = store.chunks_missing_embedding("qwen3-0.6b");
    REQUIRE(missing.size() == 1);
    REQUIRE(missing[0] == b.content_hash.to_hex());
}

TEST_CASE("Store: reopen sees prior chunks and rebuilds the vector index", "[store]") {
    auto path = tmp_db_path();

    auto c = make_chunk("persist", "def persist(): pass");
    {
        auto store = Store::open(path);
        store.put_chunk("p.py", c);
        store.put_embedding(c.content_hash.to_hex(), "qwen3-0.6b",
                            std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F});
    }

    auto store = Store::open(path);
    REQUIRE(store.chunk_count() == 1);
    REQUIRE(store.embedding_count() == 1);

    const auto hits = store.search_vectors(std::vector<float>{1.0F, 0.0F, 0.0F, 0.0F}, 1);
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].chunk_hash == c.content_hash.to_hex());
}
