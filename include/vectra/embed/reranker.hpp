// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Cross-encoder reranker built on top of llama.cpp.
//
// Where the Embedder turns a single string into a vector, the
// Reranker takes a (query, document) pair and returns a relevance
// score in [0, 1]. It runs the document through the same kind of
// causal LM as the embedder but in non-pooled mode, asks the model
// to predict "yes" or "no", and scores the relative logits at the
// final position. This is the standard formulation from the
// Qwen3-Reranker paper and matches what nearly every modern
// open-source reranker exposes.
//
// Architectural rationale (see architecture.md): rerankers
// dramatically lift retrieval quality on the top-K because they
// score each candidate jointly with the query rather than via two
// independent embeddings. The cost is one forward pass per
// candidate, so we run the reranker only over the small fused
// candidate pool the Retriever produces — never over the full index.

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vectra::embed {

struct RerankerConfig {
    // Path to a Qwen3-Reranker GGUF file. Required.
    std::filesystem::path model_path;

    // Stable identifier persisted alongside any cached scores. When
    // empty, defaults to the model filename without extension.
    std::string model_id;

    // Maximum context length the model is asked to handle. Inputs
    // longer than this are truncated to the most recent tokens; for
    // a reranker this means the document tail wins, which is the
    // right call for code where the body is more discriminative than
    // headers.
    int n_ctx = 4096;

    // Number of CPU threads. 0 lets llama.cpp pick.
    int n_threads = 0;

    // Number of layers to offload to the GPU. 0 = CPU, -1 = all on
    // GPU. The actual GPU backend is decided at compile time via the
    // VECTRA_GPU_* options.
    int n_gpu_layers = 0;

    // Instruction supplied to the model. Default matches the
    // documented Qwen3-Reranker prompt for code search.
    std::string instruct = "Given a code search query, retrieve relevant code snippets.";
};

class Reranker {
public:
    // Load `config.model_path` and prepare a scoring context. Throws
    // std::runtime_error on missing file, unsupported architecture,
    // or vocab mismatch (when the "yes" / "no" tokens cannot be
    // resolved as single-token BPE units).
    [[nodiscard]] static Reranker open(const RerankerConfig& config);

    ~Reranker();
    Reranker(const Reranker&) = delete;
    Reranker& operator=(const Reranker&) = delete;
    Reranker(Reranker&&) noexcept;
    Reranker& operator=(Reranker&&) = delete;  // contains llama_context*

    // Score one (query, document) pair. Returns a value in [0, 1]
    // where higher means more relevant.
    [[nodiscard]] float score(std::string_view query, std::string_view document) const;

    // Score a batch. Equivalent to calling score() in a loop; future
    // versions can fuse the forward passes for higher throughput.
    [[nodiscard]] std::vector<float> score_batch(std::string_view query,
                                                 std::span<const std::string_view> documents) const;

    [[nodiscard]] const std::string& model_id() const noexcept;

    // Pimpl exposed by name so internal helpers in reranker.cpp can
    // operate on it without friend declarations. The struct itself
    // is only ever defined in the .cpp.
    struct Impl;

private:
    explicit Reranker(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vectra::embed
