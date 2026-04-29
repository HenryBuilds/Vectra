// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Tests cover the symbol-only retrieval path, which works without an
// embedder. The vector-side fusion is exercised by store_test's
// search_vectors coverage; an integration test that pulls a real
// model into RRF lives outside the unit suite (it needs ~640 MB of
// model data).

#include "vectra/retrieve/retriever.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>

#include "vectra/core/chunk.hpp"
#include "vectra/core/hash.hpp"
#include "vectra/store/store.hpp"

using vectra::core::Chunk;
using vectra::core::ChunkKind;
using vectra::core::hash_string;
using vectra::core::Range;
using vectra::retrieve::RetrieveOptions;
using vectra::retrieve::Retriever;
using vectra::store::Store;

namespace {

std::filesystem::path tmp_db_path() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};

    auto base = std::filesystem::temp_directory_path() / "vectra-retrieve-test";
    std::filesystem::create_directories(base);
    auto p = base / ("retr-" + session + "-" + std::to_string(counter.fetch_add(1)) + ".db");
    std::error_code ec;
    std::filesystem::remove(p, ec);
    std::filesystem::remove(p.string() + "-wal", ec);
    std::filesystem::remove(p.string() + "-shm", ec);
    return p;
}

Chunk make_chunk(std::string symbol, std::string text, ChunkKind kind = ChunkKind::Function) {
    Chunk c;
    c.language = "python";
    c.kind = kind;
    c.symbol = std::move(symbol);
    c.range = Range{0, static_cast<std::uint32_t>(text.size()), 0, 0};
    c.content_hash = hash_string(text);
    c.text = std::move(text);
    return c;
}

}  // namespace

TEST_CASE("retriever returns symbol matches without an embedder", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    store.put_chunk("a.py", make_chunk("getUser", "def getUser(): pass"));
    store.put_chunk("b.py", make_chunk("getUserByID", "def getUserByID(): pass"));
    store.put_chunk("c.py", make_chunk("setPassword", "def setPassword(): pass"));

    Retriever r(store);

    const auto hits = r.retrieve("getUser");
    REQUIRE_FALSE(hits.empty());

    const bool has_user =
        std::any_of(hits.begin(), hits.end(), [](const auto& h) { return h.symbol == "getUser"; });
    REQUIRE(has_user);
}

TEST_CASE("retriever respects k", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    for (int i = 0; i < 8; ++i) {
        const auto sym = "fooFunction" + std::to_string(i);
        const auto body = "def fooFunction" + std::to_string(i) + "(): pass";
        store.put_chunk("file.py", make_chunk(sym, body));
    }

    Retriever r(store);
    RetrieveOptions opts;
    opts.k = 3;

    const auto hits = r.retrieve("fooFunction", opts);
    REQUIRE(hits.size() == 3);
    for (std::size_t i = 1; i < hits.size(); ++i) {
        // Scores are in descending order.
        REQUIRE(hits[i - 1].score >= hits[i].score);
    }
}

TEST_CASE("retriever surfaces file_path on each hit", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    store.put_chunk("src/main.py", make_chunk("greet", "def greet(): pass"));

    Retriever r(store);
    const auto hits = r.retrieve("greet");
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].file_path == "src/main.py");
    REQUIRE(hits[0].symbol == "greet");
    REQUIRE(hits[0].kind == ChunkKind::Function);
}

TEST_CASE("retriever returns empty for queries with no matches", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    store.put_chunk("a.py", make_chunk("alpha", "def alpha(): pass"));

    Retriever r(store);
    const auto hits = r.retrieve("nothingMatchesThis");
    REQUIRE(hits.empty());
}

TEST_CASE("retriever returns empty for empty query", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    Retriever r(store);
    REQUIRE(r.retrieve("").empty());
}
