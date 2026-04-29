// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/retrieve/retriever.hpp"

#include <algorithm>
#include <cstdint>
#include <unordered_map>

#include "vectra/embed/embedder.hpp"
#include "vectra/store/store.hpp"

namespace vectra::retrieve {

namespace {

// The k constant in the RRF formula. 60 is the value from the
// original Cormack et al. paper and is what most modern hybrid
// systems use as a default; the choice damps the contribution of
// any single retriever and is robust across query types.
constexpr double kRRFConstant = 60.0;

// One retriever's contribution at rank `position` (0-indexed).
[[nodiscard]] double rrf_contribution(double weight, std::size_t position) noexcept {
    return weight / (kRRFConstant + static_cast<double>(position) + 1.0);
}

}  // namespace

Retriever::Retriever(store::Store& store) noexcept : store_(store) {}

void Retriever::set_embedder(const embed::Embedder* embedder) noexcept {
    embedder_ = embedder;
}

std::vector<Hit> Retriever::retrieve(std::string_view query, const RetrieveOptions& opts) const {
    if (query.empty() || opts.k == 0) {
        return {};
    }

    // Per-chunk fused score, accumulated as we iterate each retriever.
    std::unordered_map<std::string, double> fused_scores;
    fused_scores.reserve(opts.candidate_pool * 2);

    // ---- Symbol search via FTS5 trigram ---------------------------------
    // Always available; this is the lexical signal for code identifiers.
    const auto symbol_hits = store_.search_symbols(query, opts.candidate_pool);
    for (std::size_t i = 0; i < symbol_hits.size(); ++i) {
        fused_scores[symbol_hits[i].chunk_hash] += rrf_contribution(opts.symbol_weight, i);
    }

    // ---- Vector ANN search ----------------------------------------------
    // Only run when an embedder is attached. With no embedder we still
    // produce useful results from the symbol channel alone.
    if (embedder_ != nullptr) {
        const auto query_vec = embedder_->embed_query(query);
        const auto vector_hits = store_.search_vectors(query_vec, opts.candidate_pool);
        for (std::size_t i = 0; i < vector_hits.size(); ++i) {
            fused_scores[vector_hits[i].chunk_hash] += rrf_contribution(opts.vector_weight, i);
        }
    }

    // ---- Fuse and rank ---------------------------------------------------
    // Move into a vector so we can sort. Stable order on ties is not
    // critical for retrieval quality; we leave the underlying hash-map
    // ordering effectively random for tied scores.
    // Copying out of the unordered_map (rather than moving) avoids
    // const_casting the key, which would be UB. The candidate_pool
    // ceiling bounds this at 2 * candidate_pool entries, so the copy
    // cost is negligible.
    std::vector<std::pair<std::string, double>> ranked;
    ranked.reserve(fused_scores.size());
    for (const auto& [hash, score] : fused_scores) {
        ranked.emplace_back(hash, score);
    }
    std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
        return a.second > b.second;
    });

    if (ranked.size() > opts.k) {
        ranked.resize(opts.k);
    }

    // ---- Materialize the chunks for the top-K ---------------------------
    // Each get_chunk is one prepared-statement step against SQLite, so
    // the cost is k lookups regardless of candidate_pool size.
    std::vector<Hit> out;
    out.reserve(ranked.size());
    for (const auto& [hash, score] : ranked) {
        const auto chunk = store_.get_chunk(hash);
        if (!chunk.has_value()) {
            // The retriever's signal pointed at a chunk that has since
            // been deleted from the store. Skip rather than fail.
            continue;
        }

        Hit h;
        h.chunk_hash = hash;
        h.file_path = chunk->file_path;
        h.symbol = chunk->symbol;
        h.kind = chunk->kind;
        h.start_row = chunk->range.start_row;
        h.end_row = chunk->range.end_row;
        h.text = chunk->text;
        h.score = static_cast<float>(score);
        out.push_back(std::move(h));
    }
    return out;
}

}  // namespace vectra::retrieve
