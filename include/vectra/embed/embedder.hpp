// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Embedding inference wrapper around llama.cpp.
//
// The Embedder is the only place in vectra-embed that knows about
// llama.cpp. It loads a GGUF model file, runs forward passes, applies
// the language-specific pooling and normalization steps that
// architecture.md prescribes, and returns L2-normalized float32
// vectors that vectra-store and vectra-retrieve can consume directly.
//
// Two design decisions deserve to live near the public API rather
// than in a CHANGELOG:
//
//   1. **Last-token pooling, not mean.** Qwen3-Embedding (our
//      default model family) is trained with last-token pooling on
//      the EOS slot. Mean pooling silently degrades retrieval
//      quality by 5–15% on code benchmarks. The Embedder always
//      configures `LLAMA_POOLING_TYPE_LAST` and never exposes a
//      knob to change this — getting it wrong is too easy.
//
//   2. **Asymmetric instruct prefix.** Qwen3-Embedding expects an
//      instruction prefix on the *query* side only. Documents are
//      embedded as raw passages. The two methods `embed_query` and
//      `embed_document` enforce this distinction at the API level
//      so callers cannot accidentally cross-wire it.

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vectra::embed {

struct EmbedderConfig {
    // Path to a GGUF model file. Required.
    std::filesystem::path model_path;

    // Stable identifier persisted alongside embeddings so a future
    // index load can detect a model swap. Defaults to the model
    // filename (without extension) when left empty.
    std::string model_id;

    // Maximum context length the model is asked to handle. Inputs
    // longer than this are truncated to the most recent tokens.
    int n_ctx = 2048;

    // Number of CPU threads. 0 means "let llama.cpp pick", which
    // currently maps to `std::thread::hardware_concurrency()`.
    int n_threads = 0;

    // Number of layers to offload to the GPU. 0 keeps everything on
    // CPU; -1 offloads as many layers as fit. The actual GPU backend
    // (CUDA / Metal / Vulkan / HIP) is decided at compile time via
    // the VECTRA_GPU_* options in the top-level CMakeLists.
    //
    // Default is -1 ("use whatever GPU the build has") because a
    // CPU-only build silently caps this at 0 inside llama.cpp, so the
    // GPU-aware default is safe across build modes and saves callers
    // from having to know whether they're on a CUDA / Metal / Vulkan
    // binary.
    int n_gpu_layers = -1;

    // Instruction prefix prepended to every query. The default
    // matches Qwen3-Embedding's documented format. Override only if
    // you understand the impact on retrieval quality.
    std::string query_instruction =
        "Given a code search query, retrieve the most relevant code chunks.";
};

class Embedder {
public:
    // Load `config.model_path` and prepare an embedding context.
    // Throws std::runtime_error on missing file, unsupported model
    // architecture, or context allocation failure.
    [[nodiscard]] static Embedder open(const EmbedderConfig& config);

    ~Embedder();
    Embedder(const Embedder&) = delete;
    Embedder& operator=(const Embedder&) = delete;
    Embedder(Embedder&&) noexcept;
    Embedder& operator=(Embedder&&) = delete;  // contains llama_context*

    // Embed a single query string. The embedder prepends the
    // configured instruction prefix before tokenizing.
    [[nodiscard]] std::vector<float> embed_query(std::string_view text) const;

    // Embed a single document chunk. No instruction prefix; the text
    // is tokenized verbatim.
    [[nodiscard]] std::vector<float> embed_document(std::string_view text) const;

    // Batched variants. The implementation feeds them through the
    // model in a single sequence-aware batch where possible, which
    // is materially faster than looping the single-input path.
    [[nodiscard]] std::vector<std::vector<float>> embed_queries(
        std::span<const std::string_view> texts) const;
    [[nodiscard]] std::vector<std::vector<float>> embed_documents(
        std::span<const std::string_view> texts) const;

    // Output dimensionality of the loaded model.
    [[nodiscard]] std::size_t dim() const noexcept;

    // Identifier persisted with each stored embedding (see
    // EmbedderConfig::model_id).
    [[nodiscard]] const std::string& model_id() const noexcept;

    // Pimpl is exposed by name so internal helpers in embedder.cpp
    // can operate on it without needing friend declarations. The
    // struct itself is only ever defined inside the .cpp; the alias
    // here is opaque from a consumer's perspective.
    struct Impl;

private:
    explicit Embedder(std::unique_ptr<Impl> impl) noexcept;
    std::unique_ptr<Impl> impl_;
};

}  // namespace vectra::embed
