// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vector_index.hpp"  // resolved via the test target's include path

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <vector>

using vectra::store::detail::VectorIndex;

namespace {

// Generate a deterministic, normalized vector so cosine distances
// behave the way the tests assume.
std::vector<float> make_vec(std::initializer_list<float> values) {
    std::vector<float> v(values);
    float norm = 0.0F;
    for (float x : v)
        norm += x * x;
    norm = std::sqrt(norm);
    if (norm > 0.0F) {
        for (auto& x : v)
            x /= norm;
    }
    return v;
}

}  // namespace

TEST_CASE("VectorIndex: empty search returns no hits", "[vector_index]") {
    VectorIndex idx(4);
    const auto query = make_vec({1, 0, 0, 0});
    const auto hits = idx.search(query, 5);
    REQUIRE(hits.empty());
    REQUIRE(idx.size() == 0);
}

TEST_CASE("VectorIndex: nearest neighbor of an inserted vector is itself", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(8);

    const auto v1 = make_vec({1, 0, 0, 0});
    const auto v2 = make_vec({0, 1, 0, 0});
    const auto v3 = make_vec({0, 0, 1, 0});
    idx.upsert(1, v1);
    idx.upsert(2, v2);
    idx.upsert(3, v3);
    REQUIRE(idx.size() == 3);

    const auto hits = idx.search(v1, 1);
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].key == 1);
    REQUIRE(hits[0].distance < 0.001F);  // cos distance to itself ≈ 0
}

TEST_CASE("VectorIndex: top-k search returns hits ordered by distance", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(8);

    idx.upsert(10, make_vec({1, 0, 0, 0}));
    idx.upsert(20, make_vec({0.9F, 0.1F, 0, 0}));
    idx.upsert(30, make_vec({0, 1, 0, 0}));

    const auto query = make_vec({1, 0, 0, 0});
    const auto hits = idx.search(query, 3);

    REQUIRE(hits.size() == 3);
    // Closest first: 10 (identical), 20 (mostly aligned), 30 (orthogonal).
    REQUIRE(hits[0].key == 10);
    REQUIRE(hits[1].key == 20);
    REQUIRE(hits[2].key == 30);
    REQUIRE(hits[0].distance <= hits[1].distance);
    REQUIRE(hits[1].distance <= hits[2].distance);
}

TEST_CASE("VectorIndex: upsert replaces an existing key in place", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(4);

    idx.upsert(1, make_vec({1, 0, 0, 0}));
    REQUIRE(idx.size() == 1);

    // Replacing the same key keeps the population at 1.
    idx.upsert(1, make_vec({0, 1, 0, 0}));
    REQUIRE(idx.size() == 1);

    // Searching with the new direction now returns the same key as
    // its own nearest neighbor.
    const auto hits = idx.search(make_vec({0, 1, 0, 0}), 1);
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].key == 1);
    REQUIRE(hits[0].distance < 0.001F);
}

TEST_CASE("VectorIndex: remove drops the key", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(4);

    idx.upsert(1, make_vec({1, 0, 0, 0}));
    idx.upsert(2, make_vec({0, 1, 0, 0}));
    REQUIRE(idx.size() == 2);

    REQUIRE(idx.remove(1));
    REQUIRE(idx.size() == 1);

    const auto hits = idx.search(make_vec({1, 0, 0, 0}), 5);
    REQUIRE(hits.size() == 1);
    REQUIRE(hits[0].key == 2);
}

TEST_CASE("VectorIndex: dim mismatch on upsert is rejected", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(2);
    REQUIRE_THROWS(idx.upsert(1, std::vector<float>{1.0F, 0.0F, 0.0F}));  // dim=3 ≠ 4
}
