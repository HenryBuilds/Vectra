// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vector_index.hpp"

#include <stdexcept>
#include <utility>

#include <fmt/format.h>

// usearch headers trip our strict /WX policy on harmless warnings
// (unused parameters, signed/unsigned comparisons, ...). Silence them
// only across this include — the rest of vector_index.cpp keeps its
// normal warning level.
#if defined(_MSC_VER)
#  pragma warning(push, 0)
#elif defined(__clang__)
#  pragma clang diagnostic push
#  pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wall"
#  pragma GCC diagnostic ignored "-Wextra"
#  pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <usearch/index_dense.hpp>
#if defined(_MSC_VER)
#  pragma warning(pop)
#elif defined(__clang__)
#  pragma clang diagnostic pop
#elif defined(__GNUC__)
#  pragma GCC diagnostic pop
#endif

namespace vectra::store::detail {

namespace ux = unum::usearch;

// Pimpl owns the usearch index and the dimension. We hide the heavy
// usearch headers from vector_index.hpp so consumers don't pull them
// in transitively.
struct VectorIndex::Impl {
    ux::index_dense_t index;
};

namespace {

[[nodiscard]] ux::index_dense_t make_index(std::uint32_t dim) {
    auto metric = ux::metric_punned_t(static_cast<std::size_t>(dim),
                                      ux::metric_kind_t::cos_k,
                                      ux::scalar_kind_t::f32_k);
    auto state  = ux::index_dense_t::make(std::move(metric));
    if (!state) {
        throw std::runtime_error(fmt::format(
            "usearch: failed to create dense index (dim={}): {}",
            dim, state.error.what() ? state.error.what() : "unknown"));
    }
    return std::move(state.index);
}

}  // namespace

VectorIndex::VectorIndex(std::uint32_t dim)
    : impl_(std::make_unique<Impl>(Impl{make_index(dim)})), dim_(dim) {}

VectorIndex::~VectorIndex() = default;

VectorIndex::VectorIndex(VectorIndex&& other) noexcept
    : impl_(std::move(other.impl_)), dim_(other.dim_) {}

VectorIndex& VectorIndex::operator=(VectorIndex&& other) noexcept {
    if (this != &other) {
        std::lock_guard<std::mutex> guard(mutex_);
        impl_ = std::move(other.impl_);
        dim_  = other.dim_;
    }
    return *this;
}

void VectorIndex::reserve(std::size_t capacity) {
    std::lock_guard<std::mutex> guard(mutex_);
    // index_limits_t needs both member capacity and thread counts.
    // We reserve generously on threads so concurrent add/search calls
    // don't force a reallocation. usearch's reserve() returns void;
    // it allocates lazily and signals problems through later add()
    // calls, so we have nothing to check here.
    ux::index_limits_t limits{capacity, /*threads=*/8};
    impl_->index.reserve(limits);
}

void VectorIndex::upsert(std::uint64_t key, std::span<const float> vector) {
    if (vector.size() != dim_) {
        throw std::runtime_error(fmt::format(
            "VectorIndex::upsert: vector dim {} does not match index dim {}",
            vector.size(), dim_));
    }

    std::lock_guard<std::mutex> guard(mutex_);

    // Grow on demand. usearch's add() requires that capacity exceeds
    // size; without an explicit reserve we would either fail the add
    // or — in older versions — segfault. We double when we approach
    // the limit so amortized cost stays O(1) per insert.
    const auto current_size = impl_->index.size();
    const auto current_cap  = impl_->index.capacity();
    if (current_size + 1 >= current_cap) {
        const std::size_t target = std::max<std::size_t>(current_cap * 2, 64);
        ux::index_limits_t limits{target, /*threads=*/8};
        impl_->index.reserve(limits);
    }

    // Replace semantics: drop any existing entry first. The remove is
    // a no-op when the key is absent, so we ignore its result.
    impl_->index.remove(key);

    auto added = impl_->index.add(key, vector.data());
    if (!added) {
        throw std::runtime_error(fmt::format(
            "usearch: add(key={}) failed: {}",
            key, added.error.what() ? added.error.what() : "unknown"));
    }
}

bool VectorIndex::remove(std::uint64_t key) {
    std::lock_guard<std::mutex> guard(mutex_);
    auto removed = impl_->index.remove(key);
    if (!removed) {
        // remove() reports failure via the labeling_result_t; we treat
        // "key absent" as a benign false return rather than an error.
        return false;
    }
    return removed.completed > 0;
}

std::vector<VectorIndex::Hit> VectorIndex::search(std::span<const float> query,
                                                  std::size_t            k) const {
    if (query.size() != dim_) {
        throw std::runtime_error(fmt::format(
            "VectorIndex::search: query dim {} does not match index dim {}",
            query.size(), dim_));
    }

    std::lock_guard<std::mutex> guard(mutex_);
    // usearch's search dereferences internal storage that may not be
    // initialized when the index is empty; short-circuit that path.
    if (impl_->index.size() == 0) return {};

    auto result = impl_->index.search(query.data(), k);
    if (!result) {
        throw std::runtime_error(fmt::format(
            "usearch: search failed: {}",
            result.error.what() ? result.error.what() : "unknown"));
    }

    std::vector<Hit> hits;
    hits.reserve(result.size());
    for (std::size_t i = 0; i < result.size(); ++i) {
        auto match = result[i];
        Hit  h;
        h.key      = static_cast<std::uint64_t>(match.member.key);
        h.distance = match.distance;
        hits.push_back(h);
    }
    return hits;
}

std::size_t VectorIndex::size() const noexcept {
    std::lock_guard<std::mutex> guard(mutex_);
    return impl_->index.size();
}

}  // namespace vectra::store::detail
