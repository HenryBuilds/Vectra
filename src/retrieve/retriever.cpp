// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/retrieve/retriever.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <unordered_map>

#include "vectra/embed/embedder.hpp"
#include "vectra/embed/reranker.hpp"
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

// Tiny helper to time a block and forward to the user callback.
class StageTimer {
public:
    StageTimer() noexcept : start_(std::chrono::steady_clock::now()) {}
    [[nodiscard]] std::chrono::milliseconds elapsed() const noexcept {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_);
    }

private:
    std::chrono::steady_clock::time_point start_;
};

}  // namespace

Retriever::Retriever(store::Store& store) noexcept : store_(store) {}

void Retriever::set_embedder(const embed::Embedder* embedder) noexcept {
    embedder_ = embedder;
}

void Retriever::set_reranker(const embed::Reranker* reranker) noexcept {
    reranker_ = reranker;
}

std::vector<Hit> Retriever::retrieve(std::string_view query, const RetrieveOptions& opts) const {
    if (query.empty() || opts.k == 0) {
        return {};
    }

    const auto stage = [&](std::string_view name, std::size_t count, StageTimer& t) {
        if (opts.on_stage) {
            opts.on_stage(name, count, t.elapsed());
        }
    };

    // Per-chunk fused score, accumulated as we iterate each retriever.
    std::unordered_map<std::string, double> fused_scores;
    fused_scores.reserve(opts.candidate_pool * 2);

    // FTS5 rank-0 hash lives outside the symbol-search block so the
    // dominance check after fusion can pin it back to the top.
    std::string dominant_symbol_hash;

    // ---- Symbol search via FTS5 trigram ---------------------------------
    // Always available; this is the lexical signal for code identifiers.
    {
        StageTimer t;
        const auto symbol_hits = store_.search_symbols(query, opts.candidate_pool);
        for (std::size_t i = 0; i < symbol_hits.size(); ++i) {
            fused_scores[symbol_hits[i].chunk_hash] += rrf_contribution(opts.symbol_weight, i);
        }
        // Dominance signal: when rank-0's BM25 is clearly better than
        // rank-1's, the FTS5 channel is highly confident. SQLite's
        // bm25() returns *negative* numbers; more negative is better.
        // "Dominant" here means rank-0 is at least
        // `symbol_dominance_ratio`× as good as rank-1.
        if (opts.protect_dominant_symbol_hit && symbol_hits.size() >= 2) {
            const double r0 = symbol_hits[0].score;
            const double r1 = symbol_hits[1].score;
            if (r0 < 0.0 && r1 < 0.0 && r0 < r1 * opts.symbol_dominance_ratio) {
                dominant_symbol_hash = symbol_hits[0].chunk_hash;
            }
        } else if (opts.protect_dominant_symbol_hit && symbol_hits.size() == 1) {
            // Single-hit scenario — that one chunk is by definition
            // the FTS5 answer.
            dominant_symbol_hash = symbol_hits[0].chunk_hash;
        }
        stage("symbol search (FTS5)", symbol_hits.size(), t);
    }

    // ---- Vector ANN search ----------------------------------------------
    // Only run when an embedder is attached. With no embedder we still
    // produce useful results from the symbol channel alone.
    if (embedder_ != nullptr) {
        std::vector<float> query_vec;
        {
            StageTimer t;
            query_vec = embedder_->embed_query(query);
            stage("embed query", query_vec.size(), t);
        }
        StageTimer t;
        const auto vector_hits = store_.search_vectors(query_vec, opts.candidate_pool);
        for (std::size_t i = 0; i < vector_hits.size(); ++i) {
            fused_scores[vector_hits[i].chunk_hash] += rrf_contribution(opts.vector_weight, i);
        }
        stage("vector search (HNSW)", vector_hits.size(), t);
    }

    // ---- Fuse and rank ---------------------------------------------------
    // Move into a vector so we can sort. Stable order on ties is not
    // critical for retrieval quality; we leave the underlying hash-map
    // ordering effectively random for tied scores.
    std::vector<std::pair<std::string, double>> ranked;
    {
        StageTimer t;
        ranked.reserve(fused_scores.size());
        for (const auto& [hash, score] : fused_scores) {
            ranked.emplace_back(hash, score);
        }
        std::sort(ranked.begin(), ranked.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });

        // Pin the dominant FTS5 hit to position 0. Without this, a
        // confident exact-symbol match can be pushed out of the top-K
        // by RRF dilution from the vector channel — measured on the
        // typeflow `executor-registry` query, where hybrid took 59 s
        // vs 17 s for symbol-only because the right chunk dropped to
        // rank ~7. Implemented as a swap, not a score-bump, so the
        // visible fusion_score on the returned Hit stays meaningful.
        if (!dominant_symbol_hash.empty() && !ranked.empty() &&
            ranked.front().first != dominant_symbol_hash) {
            const auto it = std::find_if(ranked.begin(), ranked.end(), [&](const auto& p) {
                return p.first == dominant_symbol_hash;
            });
            if (it != ranked.end()) {
                std::iter_swap(ranked.begin(), it);
            }
        }

        // Adaptive top-K. Two conditions both have to hold before we
        // trim below opts.k:
        //   (a) `adaptive_k` is on (caller opted in)
        //   (b) the FTS5 channel showed a *dominant* rank-0 hit
        //       (same signal `protect_dominant_symbol_hit` uses)
        // Without (b), the ranked list is "evenly informative" and
        // the caller's k is the right size. The earlier behaviour
        // (cliff_ratio alone) was too aggressive on queries with
        // diffuse keyword matches: the typeflow workflow-queue and
        // credential-decrypt tasks regressed +174% / +198% because
        // adaptive_k cut chunks claude actually needed. Adding the
        // dominance gate stops that without rolling the feature back.
        std::size_t cutoff = opts.k;
        if (opts.adaptive_k && !dominant_symbol_hash.empty() && ranked.size() >= 2) {
            std::size_t i = 1;
            for (; i < std::min(opts.k, ranked.size()); ++i) {
                if (ranked[i - 1].second <= 0.0)
                    break;  // can't compute ratio
                const double ratio = ranked[i].second / ranked[i - 1].second;
                if (ratio < opts.adaptive_cliff_ratio) {
                    break;  // cliff: drop everything from i onward
                }
            }
            cutoff = std::max(opts.min_k, i);
            cutoff = std::min(cutoff, opts.k);
        }

        // Without a reranker, only the top-K chunks need to be looked
        // up. With a reranker we keep up to candidate_pool candidates
        // for the cross-encoder pass, then truncate to k afterwards.
        const std::size_t pool_size = (reranker_ != nullptr)
                                          ? std::min(opts.candidate_pool, ranked.size())
                                          : std::min(cutoff, ranked.size());
        if (ranked.size() > pool_size) {
            ranked.resize(pool_size);
        }
        stage("RRF fusion", ranked.size(), t);
    }

    // ---- Materialize the candidate chunks --------------------------------
    // Each get_chunk is one prepared-statement step against SQLite.
    std::vector<Hit> out;
    {
        StageTimer t;
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
            h.fusion_score = static_cast<float>(score);
            h.score = h.fusion_score;
            out.push_back(std::move(h));
        }
        stage("materialize chunks", out.size(), t);
    }

    // ---- Optional cross-encoder reranking --------------------------------
    // Score each surviving candidate jointly with the query and
    // re-sort by the reranker's relevance score. The fusion_score is
    // preserved on each Hit for callers that want to compare.
    if (reranker_ != nullptr && !out.empty()) {
        StageTimer t;
        for (auto& h : out) {
            h.score = reranker_->score(query, h.text);
        }
        std::sort(
            out.begin(), out.end(), [](const Hit& a, const Hit& b) { return a.score > b.score; });
        if (out.size() > opts.k) {
            out.resize(opts.k);
        }
        stage("rerank (cross-encoder)", out.size(), t);
    }

    return out;
}

}  // namespace vectra::retrieve
