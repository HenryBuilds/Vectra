// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/embed/embedder.hpp"

#include <fmt/format.h>

#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

// llama.cpp's public header trips our strict warning policy. Wrap the
// include so the rest of vectra-embed keeps -Werror without false
// positives from upstream.
#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include <llama.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace vectra::embed {

namespace {

// llama.cpp requires llama_backend_init() to be called once per
// process, and — separately — needs ggml_backend_load_all() to
// discover dynamic backend DLLs (ggml-cuda.dll, ggml-vulkan.dll,
// etc.) shipped alongside the executable. Without that explicit
// load call, only the statically-linked CPU backend is registered
// and llama.cpp silently falls back to CPU even when ggml-cuda.dll
// is present on disk. We guard with std::call_once so multiple
// Embedders in the same process are safe. The matching
// backend_free is intentionally omitted: process exit reclaims the
// resources, and calling it from a static destructor would race
// with other Embedders still in use.
void ensure_backend_initialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        // Run before the log silencer so the one-time
        // "loaded CUDA backend from ..." line surfaces on stderr —
        // useful for verifying GPU support actually engaged.
        ggml_backend_load_all();
        llama_backend_init();
        // Suppress llama.cpp's verbose stdout/stderr logging from
        // here on. Callers that want it can install their own
        // callback after the first Embedder::open call.
        llama_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr);
    });
}

void l2_normalize(std::vector<float>& v) {
    double sum = 0.0;
    for (float x : v) {
        sum += static_cast<double>(x) * static_cast<double>(x);
    }
    const double norm = std::sqrt(sum);
    if (norm > 0.0) {
        const double inv = 1.0 / norm;
        for (auto& x : v) {
            x = static_cast<float>(static_cast<double>(x) * inv);
        }
    }
}

// llama_tokenize signals "buffer too small" via a negative return
// equal to -required_size. We grow once and retry; a second short
// return indicates a real error.
[[nodiscard]] std::vector<llama_token> tokenize(const llama_vocab* vocab,
                                                std::string_view text,
                                                bool add_special) {
    const int32_t initial = static_cast<int32_t>(text.size()) + 32;
    std::vector<llama_token> tokens(static_cast<std::size_t>(initial));
    int32_t n = llama_tokenize(vocab,
                               text.data(),
                               static_cast<int32_t>(text.size()),
                               tokens.data(),
                               initial,
                               add_special,
                               /*parse_special=*/false);
    if (n < 0) {
        const int32_t needed = -n;
        tokens.resize(static_cast<std::size_t>(needed));
        n = llama_tokenize(vocab,
                           text.data(),
                           static_cast<int32_t>(text.size()),
                           tokens.data(),
                           needed,
                           add_special,
                           /*parse_special=*/false);
    }
    if (n < 0) {
        throw std::runtime_error("llama_tokenize failed");
    }
    tokens.resize(static_cast<std::size_t>(n));
    return tokens;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Embedder::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    int32_t n_embd = 0;
    int32_t n_ctx = 0;
    std::string model_id;
    std::string query_instruction;

    ~Impl() {
        if (ctx != nullptr) {
            llama_free(ctx);
        }
        if (model != nullptr) {
            llama_model_free(model);
        }
    }
};

namespace {

// Embed one tokenized sequence and return an L2-normalized vector.
[[nodiscard]] std::vector<float> embed_one(const Embedder::Impl& impl,
                                           std::string_view text,
                                           bool add_special) {
    auto tokens = tokenize(impl.vocab, text, add_special);
    if (tokens.empty()) {
        return std::vector<float>(static_cast<std::size_t>(impl.n_embd), 0.0F);
    }

    // Truncate to the most recent n_ctx tokens. Older content is
    // dropped; for code chunks at the model's context size this is
    // a graceful degradation.
    if (static_cast<int32_t>(tokens.size()) > impl.n_ctx) {
        const std::size_t excess = tokens.size() - static_cast<std::size_t>(impl.n_ctx);
        tokens.erase(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(excess));
    }

    llama_batch batch = llama_batch_init(static_cast<int32_t>(tokens.size()),
                                         /*embd=*/0,
                                         /*n_seq_max=*/1);
    batch.n_tokens = static_cast<int32_t>(tokens.size());
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        batch.token[i] = tokens[i];
        batch.pos[i] = static_cast<llama_pos>(i);
        batch.n_seq_id[i] = 1;
        batch.seq_id[i][0] = 0;
        // Last-token pooling only needs logits flagged on the final
        // position; the rest of the sequence still contributes via
        // attention but we save a copy out of the model.
        batch.logits[i] = (i + 1 == tokens.size()) ? 1 : 0;
    }

    llama_memory_clear(llama_get_memory(impl.ctx), /*data=*/true);

    const int32_t rc = llama_decode(impl.ctx, batch);
    if (rc != 0) {
        llama_batch_free(batch);
        throw std::runtime_error(fmt::format("llama_decode returned {}", rc));
    }

    const float* embd = llama_get_embeddings_seq(impl.ctx, /*seq_id=*/0);
    if (embd == nullptr) {
        llama_batch_free(batch);
        throw std::runtime_error("llama_get_embeddings_seq returned null");
    }

    std::vector<float> out(embd, embd + impl.n_embd);
    llama_batch_free(batch);
    l2_normalize(out);
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Embedder::Embedder(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Embedder::~Embedder() = default;
Embedder::Embedder(Embedder&&) noexcept = default;

Embedder Embedder::open(const EmbedderConfig& config) {
    ensure_backend_initialized();

    if (config.model_path.empty() || !std::filesystem::is_regular_file(config.model_path)) {
        throw std::runtime_error(
            fmt::format("Embedder: model file not found at '{}'", config.model_path.string()));
    }

    auto impl = std::make_unique<Impl>();
    impl->n_ctx = config.n_ctx;
    impl->query_instruction = config.query_instruction;
    impl->model_id = config.model_id.empty() ? config.model_path.stem().string() : config.model_id;

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;

    impl->model = llama_model_load_from_file(config.model_path.string().c_str(), model_params);
    if (impl->model == nullptr) {
        throw std::runtime_error(fmt::format("Embedder: llama_model_load_from_file failed for '{}'",
                                             config.model_path.string()));
    }
    impl->vocab = llama_model_get_vocab(impl->model);
    impl->n_embd = llama_model_n_embd(impl->model);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_threads = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads;
    ctx_params.embeddings = true;
    // Last-token pooling is the only correct choice for Qwen3-Embedding
    // (and most other modern embedding models). architecture.md
    // explains why; the API never exposes a knob to change this so
    // a future contributor cannot silently regress retrieval quality.
    ctx_params.pooling_type = LLAMA_POOLING_TYPE_LAST;
    // Batched embedding sizing — three interlocking knobs:
    //
    //   n_seq_max  : sequences that can coexist in the KV cache.
    //                Default 1 silently rejects multi-seq batches
    //                (llama_decode returns -1, no hint).
    //   n_ctx      : *total* KV cache cells in unified mode. llama
    //                splits this evenly into per-seq slots, so each
    //                sequence gets n_ctx / n_seq_max cells. If a
    //                sequence has more tokens than its slot,
    //                find_slot() fails with "n_tokens > size".
    //   n_batch /  : tokens per llama_decode call (logical /
    //   n_ubatch    physical). Defaults split a packed batch into
    //                512-token micro-batches on the GPU, which
    //                hides most of the batching win.
    //
    // Sizing target: ≤32 sequences per decode, each capped at
    // config.n_ctx tokens. 16 seqs × 2048 ctx = 32768 cells → ~3.6 GB
    // of f16 KV cache for Qwen3-Embedding-0.6B at 28 layers, which
    // fits next to the model on a 12 GB consumer GPU.
    ctx_params.n_seq_max = 16;
    ctx_params.n_ctx = static_cast<uint32_t>(config.n_ctx) * ctx_params.n_seq_max;
    ctx_params.n_batch = static_cast<uint32_t>(config.n_ctx) * ctx_params.n_seq_max;
    ctx_params.n_ubatch = ctx_params.n_batch;

    impl->ctx = llama_init_from_model(impl->model, ctx_params);
    if (impl->ctx == nullptr) {
        throw std::runtime_error(
            "Embedder: llama_init_from_model failed (out of memory or unsupported model?)");
    }

    return Embedder(std::move(impl));
}

// ---------------------------------------------------------------------------
// Embedding entry points
// ---------------------------------------------------------------------------

std::vector<float> Embedder::embed_query(std::string_view text) const {
    // Asymmetric instruct prefix: queries get the instruction, documents
    // do not. This matches Qwen3-Embedding's training format.
    std::string buf;
    buf.reserve(impl_->query_instruction.size() + text.size() + 32);
    buf.append("Instruct: ");
    buf.append(impl_->query_instruction);
    buf.append("\nQuery: ");
    buf.append(text);
    return embed_one(*impl_, buf, /*add_special=*/true);
}

std::vector<float> Embedder::embed_document(std::string_view text) const {
    return embed_one(*impl_, text, /*add_special=*/true);
}

std::vector<std::vector<float>> Embedder::embed_queries(
    std::span<const std::string_view> texts) const {
    std::vector<std::vector<float>> out;
    out.reserve(texts.size());
    for (const auto& t : texts) {
        out.push_back(embed_query(t));
    }
    return out;
}

std::vector<std::vector<float>> Embedder::embed_documents(
    std::span<const std::string_view> texts) const {
    // Pack multiple sequences into one llama_decode call. Decoding
    // 32 short chunks individually is dominated by per-call overhead
    // (memory clear, kernel launch, PCIe sync), so on GPU the
    // serial-loop version barely beat the CPU path. Bundling
    // sequences into a single batch amortizes that overhead and
    // lets the GPU run at saturation. n_batch defaults to 2048
    // tokens in llama_context, so we pack up to that and split
    // across multiple decodes when the input exceeds it.
    // Must stay <= ctx_params.n_seq_max (set at Embedder::open) and
    // sized so kBatchSeqLimit × per-seq ctx fits in n_batch.
    constexpr int32_t kBatchSeqLimit = 16;
    const int32_t n_ctx = impl_->n_ctx;
    const int32_t kBatchTokenLimit = n_ctx * kBatchSeqLimit;

    // Tokenize everything up front. Empty inputs map to zero
    // vectors and never enter a batch (matches embed_one's
    // single-input semantics).
    std::vector<std::vector<llama_token>> tokenized(texts.size());
    std::vector<std::size_t> live_indices;
    live_indices.reserve(texts.size());
    for (std::size_t i = 0; i < texts.size(); ++i) {
        auto toks = tokenize(impl_->vocab, texts[i], /*add_special=*/true);
        if (toks.empty()) {
            continue;
        }
        if (static_cast<int32_t>(toks.size()) > n_ctx) {
            const std::size_t excess = toks.size() - static_cast<std::size_t>(n_ctx);
            toks.erase(toks.begin(), toks.begin() + static_cast<std::ptrdiff_t>(excess));
        }
        tokenized[i] = std::move(toks);
        live_indices.push_back(i);
    }

    std::vector<std::vector<float>> out(texts.size());
    for (auto& v : out) {
        v.assign(static_cast<std::size_t>(impl_->n_embd), 0.0F);
    }

    // Greedy pack: keep adding sequences to the current batch until
    // the next one would push total tokens past the limit. A single
    // sequence at the n_ctx ceiling fits by construction (n_ctx <=
    // kBatchTokenLimit on every shipped config).
    std::size_t cursor = 0;
    while (cursor < live_indices.size()) {
        int32_t total_tokens = 0;
        std::size_t end = cursor;
        while (end < live_indices.size()) {
            const auto sz = static_cast<int32_t>(tokenized[live_indices[end]].size());
            const auto seq_count = static_cast<int32_t>(end - cursor);
            if (end > cursor &&
                (total_tokens + sz > kBatchTokenLimit || seq_count >= kBatchSeqLimit)) {
                break;
            }
            total_tokens += sz;
            ++end;
        }

        const int32_t n_seqs = static_cast<int32_t>(end - cursor);
        llama_batch batch = llama_batch_init(total_tokens, /*embd=*/0, /*n_seq_max=*/n_seqs);
        batch.n_tokens = total_tokens;

        int32_t pos = 0;
        for (int32_t s = 0; s < n_seqs; ++s) {
            const auto& toks = tokenized[live_indices[cursor + static_cast<std::size_t>(s)]];
            for (std::size_t i = 0; i < toks.size(); ++i) {
                batch.token[pos] = toks[i];
                batch.pos[pos] = static_cast<llama_pos>(i);
                batch.n_seq_id[pos] = 1;
                batch.seq_id[pos][0] = static_cast<llama_seq_id>(s);
                // Last-token pooling needs logits flagged on the
                // final position of each sequence.
                batch.logits[pos] = (i + 1 == toks.size()) ? 1 : 0;
                ++pos;
            }
        }

        llama_memory_clear(llama_get_memory(impl_->ctx), /*data=*/true);
        const int32_t rc = llama_decode(impl_->ctx, batch);
        if (rc != 0) {
            llama_batch_free(batch);
            throw std::runtime_error(fmt::format("llama_decode (batched) returned {}", rc));
        }

        for (int32_t s = 0; s < n_seqs; ++s) {
            const float* embd = llama_get_embeddings_seq(impl_->ctx, s);
            if (embd == nullptr) {
                llama_batch_free(batch);
                throw std::runtime_error(
                    fmt::format("llama_get_embeddings_seq(seq={}) returned null", s));
            }
            std::vector<float> v(embd, embd + impl_->n_embd);
            l2_normalize(v);
            out[live_indices[cursor + static_cast<std::size_t>(s)]] = std::move(v);
        }

        llama_batch_free(batch);
        cursor = end;
    }

    return out;
}

std::size_t Embedder::dim() const noexcept {
    return static_cast<std::size_t>(impl_->n_embd);
}

const std::string& Embedder::model_id() const noexcept {
    return impl_->model_id;
}

}  // namespace vectra::embed
