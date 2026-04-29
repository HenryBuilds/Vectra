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
