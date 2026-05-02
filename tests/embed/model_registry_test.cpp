// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/embed/model_registry.hpp"

#include <catch2/catch_test_macros.hpp>

using vectra::embed::ModelEntry;
using vectra::embed::ModelRegistry;

TEST_CASE("registry exposes the documented Qwen3-Embedding family", "[model-registry]") {
    const auto entries = ModelRegistry::all();
    REQUIRE(entries.size() >= 3);

    for (const auto* expected : {"qwen3-embed-0.6b", "qwen3-embed-4b", "qwen3-embed-8b"}) {
        const auto* e = ModelRegistry::by_name(expected);
        REQUIRE(e != nullptr);
        REQUIRE(e->name == expected);
        REQUIRE_FALSE(e->hf_repo.empty());
        REQUIRE_FALSE(e->hf_file.empty());
        REQUIRE(e->dim > 0);
    }
}

TEST_CASE("registry returns nullptr for unknown names", "[model-registry]") {
    REQUIRE(ModelRegistry::by_name("not-a-real-model") == nullptr);
    REQUIRE(ModelRegistry::by_name("") == nullptr);
}

TEST_CASE("download_url follows HuggingFace's resolve/main convention", "[model-registry]") {
    const auto* e = ModelRegistry::by_name("qwen3-embed-0.6b");
    REQUIRE(e != nullptr);

    const auto url = ModelRegistry::download_url(*e);
    REQUIRE(url.starts_with("https://huggingface.co/"));
    REQUIRE(url.find("/resolve/main/") != std::string::npos);
    REQUIRE(url.ends_with(e->hf_file));
}

TEST_CASE("local_path lives under the per-user cache directory", "[model-registry]") {
    const auto* e = ModelRegistry::by_name("qwen3-embed-0.6b");
    REQUIRE(e != nullptr);

    const auto path = ModelRegistry::local_path(*e);
    const auto cache = ModelRegistry::cache_dir();

    // The model path must be a descendant of the cache directory and
    // the file name component must match the registry entry.
    REQUIRE(path.string().starts_with(cache.string()));
    REQUIRE(path.filename().string() == e->hf_file);
}

TEST_CASE("registry includes the documented reranker", "[model-registry]") {
    const auto* e = ModelRegistry::by_name("qwen3-rerank-0.6b");
    REQUIRE(e != nullptr);
    REQUIRE_FALSE(e->hf_repo.empty());
    REQUIRE_FALSE(e->hf_file.empty());
}

TEST_CASE("registry name lookup is case-sensitive", "[model-registry]") {
    // Model registry names are an API contract; case-folding would
    // mask typos that are easier to surface as "unknown name" errors.
    REQUIRE(ModelRegistry::by_name("QWEN3-EMBED-0.6B") == nullptr);
    REQUIRE(ModelRegistry::by_name("Qwen3-Embed-0.6b") == nullptr);
    REQUIRE(ModelRegistry::by_name("qwen3-embed-0.6b") != nullptr);
}

TEST_CASE("local_path of two different entries lands at distinct files", "[model-registry]") {
    const auto* a = ModelRegistry::by_name("qwen3-embed-0.6b");
    const auto* b = ModelRegistry::by_name("qwen3-embed-4b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(ModelRegistry::local_path(*a) != ModelRegistry::local_path(*b));
}

TEST_CASE("download_url for different entries yields different URLs", "[model-registry]") {
    const auto* a = ModelRegistry::by_name("qwen3-embed-0.6b");
    const auto* b = ModelRegistry::by_name("qwen3-embed-8b");
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    REQUIRE(ModelRegistry::download_url(*a) != ModelRegistry::download_url(*b));
}

TEST_CASE("registry dimensions match the documented Qwen3-Embedding hidden sizes",
          "[model-registry]") {
    // The embedding dimension is part of the model's identity — the
    // store rebuilds the HNSW geometry around it on the first
    // put_embedding call. Pin the expected values so a stale
    // registry entry surfaces here rather than as a silent
    // dim-mismatch error in production.
    REQUIRE(ModelRegistry::by_name("qwen3-embed-0.6b")->dim == 1024);
    REQUIRE(ModelRegistry::by_name("qwen3-embed-4b")->dim == 2560);
    REQUIRE(ModelRegistry::by_name("qwen3-embed-8b")->dim == 4096);
}
