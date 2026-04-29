// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/embed/model_registry.hpp"

#include <fmt/format.h>
#include <spdlog/spdlog.h>

#include <array>
#include <cstdlib>

namespace vectra::embed {

namespace {

// Built-in catalogue. Adding a new model is a single line here.
//
// Picks reflect architecture.md's quantization policy: Q8_0 is the
// minimum acceptable quality for embedding models because lower
// quantizations collapse the fine-grained similarity geometry that
// retrieval relies on. The 8B variant is FP16 because the Q8_0
// version is functionally indistinguishable in practice and FP16 is
// easier to read on disk.
//
// sha256 fields are intentionally empty for the initial release;
// they are filled in after each file is downloaded and verified
// against the upstream announcement. The downloader logs a warning
// when no hash is provided.
const std::array<ModelEntry, 4> kBuiltInModels{{
    {
        "qwen3-embed-0.6b",
        "Qwen3-Embedding 0.6B (Q8_0) — 1024-dim, fits on CPU",
        "Qwen/Qwen3-Embedding-0.6B-GGUF",
        "Qwen3-Embedding-0.6B-Q8_0.gguf",
        /*sha256=*/"",
        /*size_bytes=*/0,
        /*dim=*/1024,
    },
    {
        "qwen3-embed-4b",
        "Qwen3-Embedding 4B (Q8_0) — 2560-dim, ~6 GB VRAM",
        "Qwen/Qwen3-Embedding-4B-GGUF",
        "Qwen3-Embedding-4B-Q8_0.gguf",
        /*sha256=*/"",
        /*size_bytes=*/0,
        /*dim=*/2560,
    },
    {
        "qwen3-embed-8b",
        "Qwen3-Embedding 8B (Q8_0) — 4096-dim, ~9 GB VRAM",
        "Qwen/Qwen3-Embedding-8B-GGUF",
        "Qwen3-Embedding-8B-Q8_0.gguf",
        /*sha256=*/"",
        /*size_bytes=*/0,
        /*dim=*/4096,
    },
    {
        // Cross-encoder reranker. Consumed by Retriever after RRF
        // fusion to score the small candidate pool jointly with the
        // query. dim is N/A for a reranker (output is a scalar per
        // pair); 0 records that explicitly.
        "qwen3-rerank-0.6b",
        "Qwen3-Reranker 0.6B (Q8_0) — cross-encoder for retrieval rerank",
        "Qwen/Qwen3-Reranker-0.6B-GGUF",
        "Qwen3-Reranker-0.6B-Q8_0.gguf",
        /*sha256=*/"",
        /*size_bytes=*/0,
        /*dim=*/0,
    },
}};

[[nodiscard]] std::filesystem::path os_default_cache_dir() {
    namespace fs = std::filesystem;

#if defined(_WIN32)
    if (const char* p = std::getenv("LOCALAPPDATA"); p != nullptr && *p != '\0') {
        return fs::path{p} / "vectra" / "Cache" / "models";
    }
#elif defined(__APPLE__)
    if (const char* p = std::getenv("HOME"); p != nullptr && *p != '\0') {
        return fs::path{p} / "Library" / "Caches" / "vectra" / "models";
    }
#else
    if (const char* p = std::getenv("XDG_CACHE_HOME"); p != nullptr && *p != '\0') {
        return fs::path{p} / "vectra" / "models";
    }
    if (const char* p = std::getenv("HOME"); p != nullptr && *p != '\0') {
        return fs::path{p} / ".cache" / "vectra" / "models";
    }
#endif

    spdlog::warn(
        "could not determine a per-user cache directory; "
        "falling back to the system temp directory");
    return fs::temp_directory_path() / "vectra" / "models";
}

}  // namespace

const ModelEntry* ModelRegistry::by_name(std::string_view name) {
    for (const auto& m : kBuiltInModels) {
        if (m.name == name)
            return &m;
    }
    return nullptr;
}

std::span<const ModelEntry> ModelRegistry::all() {
    return std::span<const ModelEntry>(kBuiltInModels.data(), kBuiltInModels.size());
}

std::filesystem::path ModelRegistry::cache_dir() {
    if (const char* env = std::getenv("VECTRA_MODEL_DIR"); env != nullptr && *env != '\0') {
        return std::filesystem::path{env};
    }
    return os_default_cache_dir();
}

std::filesystem::path ModelRegistry::local_path(const ModelEntry& entry) {
    return cache_dir() / entry.name / entry.hf_file;
}

std::string ModelRegistry::download_url(const ModelEntry& entry) {
    return fmt::format("https://huggingface.co/{}/resolve/main/{}", entry.hf_repo, entry.hf_file);
}

}  // namespace vectra::embed
