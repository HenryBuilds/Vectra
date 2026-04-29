// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/language.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>

using vectra::core::LanguageRegistry;

namespace {

LanguageRegistry load_repo_registry() {
    const std::filesystem::path repo_root = VECTRA_REPO_ROOT;
    const std::filesystem::path manifest = repo_root / "languages.toml";
    return LanguageRegistry::from_toml(manifest, repo_root);
}

}  // namespace

TEST_CASE("registry loads languages.toml without throwing", "[language]") {
    REQUIRE_NOTHROW(load_repo_registry());
}

TEST_CASE("registry exposes all eight built-in languages", "[language]") {
    const auto registry = load_repo_registry();

    REQUIRE(registry.all().size() == 8);

    for (const auto* name :
         {"c", "cpp", "python", "javascript", "typescript", "tsx", "rust", "go"}) {
        const auto* lang = registry.by_name(name);
        REQUIRE(lang != nullptr);
        REQUIRE(lang->ts_language != nullptr);
        REQUIRE_FALSE(lang->chunks_query_source.empty());
    }
}

TEST_CASE("by_extension resolves by file extension, case-insensitively", "[language]") {
    const auto registry = load_repo_registry();

    REQUIRE(registry.by_extension("py")->name == "python");
    REQUIRE(registry.by_extension("PY")->name == "python");
    REQUIRE(registry.by_extension("rs")->name == "rust");
    REQUIRE(registry.by_extension("tsx")->name == "tsx");
    REQUIRE(registry.by_extension("ts")->name == "typescript");
}

TEST_CASE("for_path strips the leading dot from the extension", "[language]") {
    const auto registry = load_repo_registry();

    REQUIRE(registry.for_path("foo/bar.py")->name == "python");
    REQUIRE(registry.for_path("LICENSE") == nullptr);
    REQUIRE(registry.for_path("script.unknown") == nullptr);
}
