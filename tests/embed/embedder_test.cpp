// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/embed/embedder.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using vectra::embed::Embedder;
using vectra::embed::EmbedderConfig;

TEST_CASE("Embedder::open rejects an empty model path", "[embed]") {
    EmbedderConfig cfg{};  // model_path stays empty
    REQUIRE_THROWS_AS(Embedder::open(cfg), std::runtime_error);
}

TEST_CASE("Embedder::open rejects a non-existent file", "[embed]") {
    EmbedderConfig cfg{};
    cfg.model_path = std::filesystem::temp_directory_path() / "vectra-no-such-model.gguf";
    // Make sure we are actually testing the error path.
    REQUIRE_FALSE(std::filesystem::exists(cfg.model_path));
    REQUIRE_THROWS_AS(Embedder::open(cfg), std::runtime_error);
}

TEST_CASE("Embedder::open rejects a path that is a directory", "[embed]") {
    EmbedderConfig cfg{};
    cfg.model_path = std::filesystem::temp_directory_path();
    REQUIRE_THROWS_AS(Embedder::open(cfg), std::runtime_error);
}

TEST_CASE("EmbedderConfig defaults carry the documented instruct prefix", "[embed]") {
    const EmbedderConfig cfg{};
    // The exact wording matches Qwen3-Embedding's documented format.
    // Tests pin it so a future diff that silently changes retrieval
    // semantics has to also update this assertion.
    REQUIRE(cfg.query_instruction.find("retrieve") != std::string::npos);
    REQUIRE(cfg.n_ctx == 2048);
    REQUIRE(cfg.n_gpu_layers == 0);
}
