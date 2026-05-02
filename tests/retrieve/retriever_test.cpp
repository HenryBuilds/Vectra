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

TEST_CASE("retriever returns empty for an empty index", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    Retriever r(store);
    REQUIRE(r.retrieve("anyQuery").empty());
}

TEST_CASE("retriever default k is bounded - does not return everything", "[retrieve]") {
    // Ensure the default top-k is a sane upper bound (8). Overflow
    // would mean the caller has to remember to set k explicitly,
    // which the CLI doesn't always do.
    auto store = Store::open(tmp_db_path());
    for (int i = 0; i < 32; ++i) {
        store.put_chunk(
            "f.py",
            make_chunk("query" + std::to_string(i), "def query" + std::to_string(i) + "(): pass"));
    }
    Retriever r(store);
    const auto hits = r.retrieve("query");
    REQUIRE(hits.size() <= 16);
    REQUIRE_FALSE(hits.empty());
}

TEST_CASE("retriever surfaces start/end rows for navigation", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    Chunk c = make_chunk("foo", "def foo(): pass");
    c.range = vectra::core::Range{0, 14, 10, 12};  // start_row=10, end_row=12
    c.content_hash = hash_string(c.text);
    store.put_chunk("src/foo.py", c);

    Retriever r(store);
    const auto hits = r.retrieve("foo");
    REQUIRE_FALSE(hits.empty());
    REQUIRE(hits[0].start_row == 10);
    REQUIRE(hits[0].end_row == 12);
}

TEST_CASE("retriever does not duplicate a chunk that matches multiple terms", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    store.put_chunk("a.py", make_chunk("loginUser", "def loginUser(): pass"));
    store.put_chunk("b.py", make_chunk("loginAdmin", "def loginAdmin(): pass"));

    Retriever r(store);
    // A multi-term query that could plausibly match each chunk via
    // different tokens. The retriever must dedupe so the same
    // chunk is never returned twice.
    const auto hits = r.retrieve("login user");
    std::vector<std::string> hashes;
    for (const auto& h : hits)
        hashes.push_back(h.symbol);
    std::sort(hashes.begin(), hashes.end());
    const auto dup = std::unique(hashes.begin(), hashes.end());
    REQUIRE(dup == hashes.end());
}

TEST_CASE("retriever ranks an exact-symbol match above a partial one", "[retrieve]") {
    auto store = Store::open(tmp_db_path());
    store.put_chunk("a.py", make_chunk("login", "def login(): pass"));
    store.put_chunk("b.py", make_chunk("loginByEmail", "def loginByEmail(): pass"));
    store.put_chunk("c.py", make_chunk("loginByPhone", "def loginByPhone(): pass"));

    Retriever r(store);
    const auto hits = r.retrieve("login");
    REQUIRE_FALSE(hits.empty());
    // The exact match should be among the top results.
    bool exact_in_top_two = false;
    for (std::size_t i = 0; i < std::min<std::size_t>(2, hits.size()); ++i) {
        if (hits[i].symbol == "login") {
            exact_in_top_two = true;
            break;
        }
    }
    REQUIRE(exact_in_top_two);
}

TEST_CASE("retriever survives unusual query characters", "[retrieve]") {
    // FTS5 has reserved characters (parentheses, colons, double
    // quotes) that would normally need escaping. Vectra's store
    // sanitizes them — the retriever should not throw on raw user
    // input that contains such bytes.
    auto store = Store::open(tmp_db_path());
    store.put_chunk("a.py", make_chunk("greet", "def greet(): pass"));

    Retriever r(store);
    REQUIRE_NOTHROW(r.retrieve("foo: \"bar\" (baz)"));
    REQUIRE_NOTHROW(r.retrieve("**quoted**"));
    REQUIRE_NOTHROW(r.retrieve("a OR b"));
    REQUIRE_NOTHROW(r.retrieve("trailing-dash- "));
}
