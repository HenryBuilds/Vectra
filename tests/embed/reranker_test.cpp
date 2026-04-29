// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Error-path coverage for the cross-encoder reranker. The actual
// scoring round-trip needs a real GGUF model and is exercised by
// the CLI smoke test before each release; the unit suite stays
// model-free so CI does not have to download 600+ MB of weights.

#include "vectra/embed/reranker.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using vectra::embed::Reranker;
using vectra::embed::RerankerConfig;

TEST_CASE("Reranker::open rejects an empty model path", "[reranker]") {
    RerankerConfig cfg{};
    REQUIRE_THROWS_AS(Reranker::open(cfg), std::runtime_error);
}

TEST_CASE("Reranker::open rejects a non-existent file", "[reranker]") {
    RerankerConfig cfg{};
    cfg.model_path = std::filesystem::temp_directory_path() / "vectra-no-such-reranker.gguf";
    REQUIRE_FALSE(std::filesystem::exists(cfg.model_path));
    REQUIRE_THROWS_AS(Reranker::open(cfg), std::runtime_error);
}

TEST_CASE("Reranker::open rejects a path that is a directory", "[reranker]") {
    RerankerConfig cfg{};
    cfg.model_path = std::filesystem::temp_directory_path();
    REQUIRE_THROWS_AS(Reranker::open(cfg), std::runtime_error);
}

TEST_CASE("RerankerConfig defaults pin the documented instruction", "[reranker]") {
    const RerankerConfig cfg{};
    REQUIRE(cfg.n_ctx == 4096);
    REQUIRE(cfg.n_gpu_layers == 0);
    REQUIRE_FALSE(cfg.instruct.empty());
}
