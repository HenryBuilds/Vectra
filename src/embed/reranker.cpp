// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/embed/reranker.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <utility>

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

void ensure_backend_initialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        // See embedder.cpp for why we need ggml_backend_load_all
        // before llama_backend_init (dynamic backend DLL discovery).
        ggml_backend_load_all();
        llama_backend_init();
        llama_log_set([](enum ggml_log_level, const char*, void*) {}, nullptr);
    });
}

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
                               /*parse_special=*/true);
    if (n < 0) {
        const int32_t needed = -n;
        tokens.resize(static_cast<std::size_t>(needed));
        n = llama_tokenize(vocab,
                           text.data(),
                           static_cast<int32_t>(text.size()),
                           tokens.data(),
                           needed,
                           add_special,
                           /*parse_special=*/true);
    }
    if (n < 0) {
        throw std::runtime_error("llama_tokenize failed");
    }
    tokens.resize(static_cast<std::size_t>(n));
    return tokens;
}

// Resolve the BPE token ID that the model emits for the literal
// string `text`. Throws when the tokenizer splits it into more than
// one piece — that means the chosen decision tokens are not a clean
// fit for this model and we cannot score reliably against them.
[[nodiscard]] llama_token single_token(const llama_vocab* vocab, std::string_view text) {
    auto tokens = tokenize(vocab, text, /*add_special=*/false);
    if (tokens.size() != 1) {
        throw std::runtime_error(fmt::format(
            "Reranker: expected '{}' to tokenize as a single token, got {}", text, tokens.size()));
    }
    return tokens.front();
}

// The Qwen3-Reranker prompt format. Trailing "<think>\n\n</think>"
// is the documented "no chain-of-thought" prefix that primes the
// model to emit a single yes/no token next.
[[nodiscard]] std::string format_prompt(std::string_view instruct,
                                        std::string_view query,
                                        std::string_view document) {
    return fmt::format(
        "<|im_start|>system\n"
        "Judge whether the Document meets the requirements based on the Query "
        "and the Instruct provided. Note that the answer can only be \"yes\" "
        "or \"no\".<|im_end|>\n"
        "<|im_start|>user\n"
        "<Instruct>: {}\n"
        "<Query>: {}\n"
        "<Document>: {}<|im_end|>\n"
        "<|im_start|>assistant\n"
        "<think>\n\n</think>\n\n",
        instruct,
        query,
        document);
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------

struct Reranker::Impl {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    llama_token yes_token = 0;
    llama_token no_token = 0;
    int32_t n_ctx = 0;
    std::string model_id;
    std::string instruct;

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

[[nodiscard]] float score_one(const Reranker::Impl& impl,
                              std::string_view query,
                              std::string_view document) {
    const auto prompt = format_prompt(impl.instruct, query, document);
    auto tokens = tokenize(impl.vocab, prompt, /*add_special=*/false);
    if (tokens.empty()) {
        return 0.5F;  // degenerate input, neutral score
    }

    // Truncate to the most recent n_ctx tokens. For a reranker the
    // tail (document body) carries the discriminative signal we
    // care about; cutting from the front is the right policy.
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
        // Only the final position needs logits — that is where the
        // model would emit "yes" or "no" next.
        batch.logits[i] = (i + 1 == tokens.size()) ? 1 : 0;
    }

    llama_memory_clear(llama_get_memory(impl.ctx), /*data=*/true);

    const int32_t rc = llama_decode(impl.ctx, batch);
    if (rc != 0) {
        llama_batch_free(batch);
        throw std::runtime_error(fmt::format("llama_decode returned {} in reranker", rc));
    }

    const float* logits = llama_get_logits_ith(impl.ctx, batch.n_tokens - 1);
    if (logits == nullptr) {
        llama_batch_free(batch);
        throw std::runtime_error("llama_get_logits_ith returned null");
    }

    const float yes = logits[impl.yes_token];
    const float no = logits[impl.no_token];
    llama_batch_free(batch);

    // Numerically stable two-way softmax. Equivalent to sigmoid of
    // (yes - no), but written as softmax for clarity.
    const float max_logit = std::max(yes, no);
    const float ey = std::exp(yes - max_logit);
    const float en = std::exp(no - max_logit);
    return ey / (ey + en);
}

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

Reranker::Reranker(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
Reranker::~Reranker() = default;
Reranker::Reranker(Reranker&&) noexcept = default;

Reranker Reranker::open(const RerankerConfig& config) {
    ensure_backend_initialized();

    if (config.model_path.empty() || !std::filesystem::is_regular_file(config.model_path)) {
        throw std::runtime_error(
            fmt::format("Reranker: model file not found at '{}'", config.model_path.string()));
    }

    auto impl = std::make_unique<Impl>();
    impl->n_ctx = config.n_ctx;
    impl->instruct = config.instruct;
    impl->model_id = config.model_id.empty() ? config.model_path.stem().string() : config.model_id;

    llama_model_params model_params = llama_model_default_params();
    model_params.n_gpu_layers = config.n_gpu_layers;
    impl->model = llama_model_load_from_file(config.model_path.string().c_str(), model_params);
    if (impl->model == nullptr) {
        throw std::runtime_error(fmt::format("Reranker: llama_model_load_from_file failed for '{}'",
                                             config.model_path.string()));
    }
    impl->vocab = llama_model_get_vocab(impl->model);

    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = static_cast<uint32_t>(config.n_ctx);
    ctx_params.n_threads = config.n_threads;
    ctx_params.n_threads_batch = config.n_threads;
    // Crucially: NO embeddings mode. We want raw logits so we can
    // pick out the "yes" / "no" probabilities at the final token.
    ctx_params.embeddings = false;
    ctx_params.pooling_type = LLAMA_POOLING_TYPE_NONE;

    impl->ctx = llama_init_from_model(impl->model, ctx_params);
    if (impl->ctx == nullptr) {
        throw std::runtime_error(
            "Reranker: llama_init_from_model failed (out of memory or unsupported model?)");
    }

    impl->yes_token = single_token(impl->vocab, "yes");
    impl->no_token = single_token(impl->vocab, "no");

    return Reranker(std::move(impl));
}

float Reranker::score(std::string_view query, std::string_view document) const {
    return score_one(*impl_, query, document);
}

std::vector<float> Reranker::score_batch(std::string_view query,
                                         std::span<const std::string_view> documents) const {
    std::vector<float> out;
    out.reserve(documents.size());
    for (const auto& d : documents) {
        out.push_back(score_one(*impl_, query, d));
    }
    return out;
}

const std::string& Reranker::model_id() const noexcept {
    return impl_->model_id;
}

}  // namespace vectra::embed
