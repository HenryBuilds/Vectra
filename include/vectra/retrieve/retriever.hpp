// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Hybrid retrieval: combines symbol-level BM25 (FTS5 trigram) with
// vector-level ANN (usearch HNSW) into a single ranked list via
// Reciprocal Rank Fusion. Optional embedder and reranker
// dependencies mean the retriever degrades gracefully:
//
//   - With neither: symbol-only search via FTS5.
//   - With embedder only: hybrid RRF.
//   - With embedder + reranker: hybrid RRF, then a cross-encoder
//     pass that re-scores the top candidate_pool jointly with the
//     query before truncating to k.

#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/core/chunk.hpp"

namespace vectra::store {
class Store;
}

namespace vectra::embed {
class Embedder;
class Reranker;
}  // namespace vectra::embed

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

    // Higher is better. Carries the cross-encoder relevance score
    // when a reranker was attached, otherwise the RRF fusion score.
    float score = 0.0F;
    // Always set to the pre-rerank fusion score when a reranker is
    // active, so callers that want to compare the two signals can.
    // Equals `score` when no reranker is attached.
    float fusion_score = 0.0F;
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

    // When true, the retriever inspects the FTS5 BM25 score
    // distribution and, if the rank-0 hit is "dominant" (clearly
    // better than rank-1 by `dominance_ratio`), pins that chunk
    // into the top-K regardless of how the vector channel ranks
    // it. Fixes a measured regression where hybrid retrieval with
    // an embedder *dilutes* a strong FTS5 match — the bench data
    // showed `executor-registry` queries going from 17 s on
    // symbol-only to 59 s on hybrid because the right chunk got
    // dropped by RRF dilution. Defaults to true; set to false to
    // get the un-protected fusion behaviour.
    bool protect_dominant_symbol_hit = true;
    double symbol_dominance_ratio = 1.5;

    // Adaptive top-K: cap the returned hit count by *score gradient*
    // instead of taking exactly `k`. If the fused-score drop from
    // rank-N to rank-N+1 is steep (rank-N+1 < rank-N * cliff_ratio),
    // we stop. Bounded by [min_k, k] so degenerate gradients still
    // give the caller something to work with. Off by default
    // because callers that already know the right K (the VS Code
    // chat panel passes 8) shouldn't have it second-guessed.
    bool adaptive_k = false;
    std::size_t min_k = 2;
    double adaptive_cliff_ratio = 0.55;

    // Optional progress callback invoked after each retrieval stage
    // completes. `name` is a short human label (e.g. "symbol search",
    // "embed query"); `count` is the stage's primary count metric
    // (hits returned, candidates kept, ...) — 0 when not applicable.
    // The duration covers only the named stage. Useful for surfacing
    // a verbose pipeline view from the CLI.
    std::function<void(std::string_view name, std::size_t count, std::chrono::milliseconds dur)>
        on_stage;
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

    // Attach a cross-encoder reranker. Optional: when null, retrieve()
    // returns the RRF-fused list directly. When set, the reranker is
    // applied to the top `candidate_pool` after fusion and the final
    // ordering is by reranker score. Pointer assignment; the reranker
    // must outlive the Retriever while attached.
    void set_reranker(const embed::Reranker* reranker) noexcept;

    // Run a query against the index. Returns at most opts.k hits,
    // ordered by descending score (best first).
    [[nodiscard]] std::vector<Hit> retrieve(std::string_view query,
                                            const RetrieveOptions& opts = {}) const;

private:
    store::Store& store_;
    const embed::Embedder* embedder_ = nullptr;
    const embed::Reranker* reranker_ = nullptr;
};

}  // namespace vectra::retrieve
