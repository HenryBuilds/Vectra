// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Thin wrapper around usearch's dense HNSW index. Used by Store as
// the in-memory ANN structure; rebuilt from SQLite on open() so the
// SQLite database remains the single source of truth.
//
// Internal to vectra-store; not exposed in include/vectra/store/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace vectra::store::detail {

class VectorIndex {
public:
    struct Hit {
        std::uint64_t key = 0;
        float distance = 0.0F;
    };

    // Construct an empty index for vectors of `dim` float32 elements,
    // using cosine distance. The dimension is fixed at construction
    // time; switching to a different embedding model requires
    // discarding the index and rebuilding.
    explicit VectorIndex(std::uint32_t dim);
    ~VectorIndex();

    VectorIndex(const VectorIndex&) = delete;
    VectorIndex& operator=(const VectorIndex&) = delete;
    VectorIndex(VectorIndex&&) noexcept;
    VectorIndex& operator=(VectorIndex&&) noexcept;

    // Reserve capacity to avoid reallocation when the population is
    // known up-front (e.g. while rebuilding from SQLite).
    void reserve(std::size_t capacity);

    // Insert or replace the vector for `key`. The vector must have
    // exactly `dim()` elements; otherwise std::runtime_error is thrown.
    void upsert(std::uint64_t key, std::span<const float> vector);

    // Remove a key. Returns true if the key existed.
    bool remove(std::uint64_t key);

    // Search for the `k` nearest neighbors. Hits are ordered by
    // ascending distance (closest first).
    [[nodiscard]] std::vector<Hit> search(std::span<const float> query, std::size_t k) const;

    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::uint32_t dim() const noexcept { return dim_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::uint32_t dim_;
    mutable std::mutex mutex_;
};

}  // namespace vectra::store::detail
