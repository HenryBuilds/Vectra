// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Built-in catalogue of embedding models, plus the cache-directory
// conventions that `vectra model pull` writes into and Embedder reads
// from. Adding a new known model is a single ModelEntry — no C++
// glue elsewhere.

#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace vectra::embed {

// One entry in the built-in model registry. `hf_repo` and `hf_file`
// together yield a HuggingFace download URL of the form
//   https://huggingface.co/<hf_repo>/resolve/main/<hf_file>
//
// `sha256` may be empty during development; the downloader skips
// verification when it is. We fill them in once we have downloaded
// each file and recorded its hash.
struct ModelEntry {
    std::string name;            // canonical short name, used by CLI
    std::string description;     // single-line human description
    std::string hf_repo;         // e.g. "Qwen/Qwen3-Embedding-0.6B-GGUF"
    std::string hf_file;         // e.g. "Qwen3-Embedding-0.6B-Q8_0.gguf"
    std::string sha256;          // expected SHA256 hex (lowercase); empty = skip
    std::size_t size_bytes = 0;  // expected size; 0 = unknown
    int dim = 0;                 // embedding dimension (informational)
};

class ModelRegistry {
public:
    // Lookup a built-in model by its canonical short name (e.g.
    // "qwen3-embed-0.6b"). Returns nullptr if the name is unknown.
    [[nodiscard]] static const ModelEntry* by_name(std::string_view name);

    // Iterate every built-in entry. Order is stable across calls so
    // CLI output (`vectra model list`) is deterministic.
    [[nodiscard]] static std::span<const ModelEntry> all();

    // Resolved local cache directory. The lookup order is:
    //   1. The VECTRA_MODEL_DIR env var, if set and non-empty.
    //   2. Per-OS conventions:
    //        Linux:   $XDG_CACHE_HOME/vectra/models or ~/.cache/vectra/models
    //        macOS:   ~/Library/Caches/vectra/models
    //        Windows: %LOCALAPPDATA%\vectra\Cache\models
    //   3. Fallback: <temp>/vectra/models, with a warning logged.
    [[nodiscard]] static std::filesystem::path cache_dir();

    // Local on-disk path for a registry entry. Combines cache_dir()
    // with a per-model subdirectory and the GGUF file name. Does not
    // download — callers must ensure the file is present.
    [[nodiscard]] static std::filesystem::path local_path(const ModelEntry& entry);

    // Convenience: full HuggingFace download URL for an entry.
    [[nodiscard]] static std::string download_url(const ModelEntry& entry);
};

}  // namespace vectra::embed
