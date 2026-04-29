// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Hybrid retrieval: combines symbol-level BM25 (FTS5 trigram) with
// vector-level ANN (usearch HNSW) into a single ranked list via
// Reciprocal Rank Fusion. Optional embedder dependency means the
// retriever stays usable for symbol-only search even without a
// downloaded embedding model.
//
// A future commit will add a cross-encoder reranker between the
// fused list and the final top-K. The current Retriever returns
// fused results directly, which is already a sizeable quality lift
// over either retrieval signal alone.

#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/core/chunk.hpp"

namespace vectra::store {
class Store;
}

namespace vectra::embed {
class Embedder;
}

namespace vectra::retrieve {

// One ranked search result. Exposes the chunk content directly so
// the caller does not need a second store lookup to render the hit.
struct Hit {
    std::string chunk_hash;
    std::string file_path;
    std::string symbol;
    core::ChunkKind kind = core::ChunkKind::Unknown;
    std::uint32_t start_row = 0;
    std::uint32_t end_row = 0;
    std::string text;
    float score = 0.0F;  // RRF-fused rank score, higher is better
};

struct RetrieveOptions {
    // Final number of hits to return.
    std::size_t k = 10;

    // Per-retriever candidate pool size before fusion. Larger values
    // give RRF more material to merge but cost more time.
    std::size_t candidate_pool = 50;

    // Reciprocal Rank Fusion weights. Default (1.0, 1.0) treats both
    // signals equally. Increase the symbol weight when looking up
    // exact identifiers; the vector weight when asking semantic
    // questions.
    double vector_weight = 1.0;
    double symbol_weight = 1.0;
};

class Retriever {
public:
    // Construct against a backing store. The store reference must
    // outlive the Retriever.
    explicit Retriever(store::Store& store) noexcept;

    // Attach an embedder. Optional: when null, retrieve() falls back
    // to symbol-only search via FTS5. Setting / clearing the embedder
    // is a pointer assignment; the embedder reference must outlive
    // this Retriever while attached.
    void set_embedder(const embed::Embedder* embedder) noexcept;

    // Run a query against the index. Returns at most opts.k hits,
    // ordered by descending score (best first).
    [[nodiscard]] std::vector<Hit> retrieve(std::string_view query,
                                            const RetrieveOptions& opts = {}) const;

private:
    store::Store& store_;
    const embed::Embedder* embedder_ = nullptr;
};

}  // namespace vectra::retrieve
