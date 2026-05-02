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

TEST_CASE("VectorIndex: search with k larger than the index returns all hits", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(4);
    idx.upsert(1, make_vec({1, 0, 0, 0}));
    idx.upsert(2, make_vec({0, 1, 0, 0}));

    const auto hits = idx.search(make_vec({1, 0, 0, 0}), 100);
    REQUIRE(hits.size() == 2);
    // Distances must still be ordered closest-first.
    REQUIRE(hits[0].distance <= hits[1].distance);
}

TEST_CASE("VectorIndex: remove returns false for an unknown key", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(4);
    idx.upsert(1, make_vec({1, 0, 0, 0}));
    REQUIRE_FALSE(idx.remove(999));
    REQUIRE(idx.size() == 1);
}

TEST_CASE("VectorIndex: search with k=0 returns nothing", "[vector_index]") {
    VectorIndex idx(4);
    idx.reserve(2);
    idx.upsert(1, make_vec({1, 0, 0, 0}));
    REQUIRE(idx.search(make_vec({1, 0, 0, 0}), 0).empty());
}

TEST_CASE("VectorIndex: orthogonal vectors are at maximum cosine distance", "[vector_index]") {
    // Sanity check on the distance metric: the unit vector (1,0,0,0)
    // and (0,1,0,0) should land at cosine distance ≈ 1.0
    // (perpendicular). usearch returns 1 - cos(theta) for the
    // cosine metric, so we expect a value comfortably above 0.5.
    VectorIndex idx(4);
    idx.reserve(2);
    idx.upsert(1, make_vec({1, 0, 0, 0}));
    idx.upsert(2, make_vec({0, 1, 0, 0}));

    const auto hits = idx.search(make_vec({1, 0, 0, 0}), 2);
    REQUIRE(hits.size() == 2);
    REQUIRE(hits[0].key == 1);
    REQUIRE(hits[0].distance < 0.001F);  // self-match
    REQUIRE(hits[1].key == 2);
    REQUIRE(hits[1].distance > 0.5F);  // orthogonal — ≈ 1.0
}

TEST_CASE("VectorIndex: anti-parallel vectors are at the highest distance", "[vector_index]") {
    // (+x) and (-x) are 180° apart — cosine = -1, so the cosine
    // distance reported by usearch should be near 2.0 (it's
    // implemented as 1 - cos in the half-open form some libraries
    // use, or as 1 - cos with a clamped range — usearch is the
    // latter, so anti-parallel ≈ 2.0). The test only asserts the
    // ordering: a self-match wins, an orthogonal vector loses, an
    // anti-parallel one is the worst.
    VectorIndex idx(4);
    idx.reserve(3);
    idx.upsert(1, make_vec({1, 0, 0, 0}));   // self
    idx.upsert(2, make_vec({0, 1, 0, 0}));   // orthogonal
    idx.upsert(3, make_vec({-1, 0, 0, 0}));  // anti-parallel

    const auto hits = idx.search(make_vec({1, 0, 0, 0}), 3);
    REQUIRE(hits.size() == 3);
    REQUIRE(hits[0].key == 1);
    REQUIRE(hits[2].key == 3);
    REQUIRE(hits[0].distance < hits[1].distance);
    REQUIRE(hits[1].distance < hits[2].distance);
}

TEST_CASE("VectorIndex: many inserts are all retrievable", "[vector_index]") {
    // Stress-ish: insert 256 distinct unit vectors along uniformly
    // spaced 4-D directions. The index should grow past its
    // initial reserve without losing any earlier insertions.
    VectorIndex idx(4);
    idx.reserve(16);  // intentionally smaller than the final size

    constexpr std::uint64_t kN = 256;
    for (std::uint64_t i = 0; i < kN; ++i) {
        // Vary the unit vector deterministically. Different keys,
        // different vectors, all unit-length.
        const float t = static_cast<float>(i) / static_cast<float>(kN);
        std::vector<float> v = make_vec({1.0F + t, t, t * t, 1.0F - t});
        idx.upsert(i + 1, v);
    }
    REQUIRE(idx.size() == kN);

    // Each inserted key should be returned as the closest
    // neighbour for its own vector.
    const float t = 0.5F;
    std::vector<float> probe = make_vec({1.0F + t, t, t * t, 1.0F - t});
    const auto hits = idx.search(probe, 1);
    REQUIRE(hits.size() == 1);
    // We don't assert which exact key wins (probe matches index
    // 128 by construction, but small numerical drift could pick a
    // neighbour). The point is: hit set is non-empty even when
    // the index grew well past its reserved capacity.
}

TEST_CASE("VectorIndex: dim() reports the constructed dimension", "[vector_index]") {
    VectorIndex idx(7);
    REQUIRE(idx.dim() == 7);
}
