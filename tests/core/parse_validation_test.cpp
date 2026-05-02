// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// End-to-end parse-validation tests. For every registered language
// we keep a small fixture file under tests/fixtures/languages/ and
// assert that:
//
//   1. The grammar parses it without error.
//   2. The chunks.scm query compiles and yields at least one chunk.
//   3. The symbols.scm and imports.scm queries compile cleanly.
//
// The chunker only consumes chunks.scm at runtime today, so symbols
// and imports queries are otherwise silent: a typo there would not
// surface until those queries are wired into a future symbol/import
// indexer. Compiling them here makes that wiring safe.

#include <tree_sitter/api.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "vectra/core/chunker.hpp"
#include "vectra/core/language.hpp"

using vectra::core::Chunker;
using vectra::core::LanguageRegistry;

namespace {

namespace fs = std::filesystem;

LanguageRegistry load_repo_registry() {
    const fs::path repo_root = VECTRA_REPO_ROOT;
    return LanguageRegistry::from_toml(repo_root / "languages.toml", repo_root);
}

// Map a registered language to its fixture filename. Kept as a flat
// switch so a typo in the language list is a compile-time-ish surprise
// (returning empty triggers a clear test failure further down).
std::string fixture_for(std::string_view language) {
    if (language == "c")
        return "sample.c";
    if (language == "cpp")
        return "sample.cpp";
    if (language == "python")
        return "sample.py";
    if (language == "javascript")
        return "sample.js";
    if (language == "typescript")
        return "sample.ts";
    if (language == "tsx")
        return "sample.tsx";
    if (language == "rust")
        return "sample.rs";
    if (language == "go")
        return "sample.go";
    if (language == "java")
        return "Sample.java";
    if (language == "ruby")
        return "sample.rb";
    if (language == "csharp")
        return "Sample.cs";
    if (language == "bash")
        return "sample.sh";
    if (language == "kotlin")
        return "sample.kt";
    if (language == "php")
        return "sample.php";
    if (language == "markdown")
        return "sample.md";
    if (language == "json")
        return "sample.json";
    if (language == "yaml")
        return "sample.yaml";
    if (language == "toml")
        return "sample.toml";
    if (language == "dockerfile")
        return "sample.dockerfile";
    if (language == "hcl")
        return "sample.tf";
    if (language == "make")
        return "sample.mk";
    if (language == "scala")
        return "sample.scala";
    if (language == "lua")
        return "sample.lua";
    if (language == "html")
        return "sample.html";
    if (language == "css")
        return "sample.css";
    if (language == "sql")
        return "sample.sql";
    if (language == "dart")
        return "sample.dart";
    if (language == "elixir")
        return "sample.ex";
    if (language == "haskell")
        return "sample.hs";
    if (language == "clojure")
        return "sample.clj";
    if (language == "r")
        return "sample.r";
    if (language == "zig")
        return "sample.zig";
    if (language == "ocaml")
        return "sample.ml";
    return {};
}

std::string read_fixture(std::string_view filename) {
    const fs::path path =
        fs::path(VECTRA_REPO_ROOT) / "tests" / "fixtures" / "languages" / filename;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.is_open());
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

}  // namespace

TEST_CASE("chunks query yields at least one chunk for each language fixture", "[language][parse]") {
    const auto* language = GENERATE(as<const char*>{},
                                    "c",
                                    "cpp",
                                    "python",
                                    "javascript",
                                    "typescript",
                                    "tsx",
                                    "rust",
                                    "go",
                                    "java",
                                    "ruby",
                                    "csharp",
                                    "bash",
                                    "kotlin",
                                    "php",
                                    "markdown",
                                    "json",
                                    "yaml",
                                    "toml",
                                    "dockerfile",
                                    "hcl",
                                    "make",
                                    "scala",
                                    "lua",
                                    "html",
                                    "css",
                                    "sql",
                                    "dart",
                                    "elixir",
                                    "haskell",
                                    "clojure",
                                    "r",
                                    "zig",
                                    "ocaml");

    INFO("language: " << language);

    const auto registry = load_repo_registry();
    const auto* lang = registry.by_name(language);
    REQUIRE(lang != nullptr);

    const std::string fixture_name = fixture_for(language);
    REQUIRE_FALSE(fixture_name.empty());

    const auto source = read_fixture(fixture_name);
    REQUIRE_FALSE(source.empty());

    Chunker chunker(registry);
    const auto chunks = chunker.chunk(source, *lang);
    REQUIRE_FALSE(chunks.empty());
}

TEST_CASE("symbols and imports queries compile cleanly for each language", "[language][parse]") {
    const auto* language = GENERATE(as<const char*>{},
                                    "c",
                                    "cpp",
                                    "python",
                                    "javascript",
                                    "typescript",
                                    "tsx",
                                    "rust",
                                    "go",
                                    "java",
                                    "ruby",
                                    "csharp",
                                    "bash",
                                    "kotlin",
                                    "php",
                                    "markdown",
                                    "json",
                                    "yaml",
                                    "toml",
                                    "dockerfile",
                                    "hcl",
                                    "make",
                                    "scala",
                                    "lua",
                                    "html",
                                    "css",
                                    "sql",
                                    "dart",
                                    "elixir",
                                    "haskell",
                                    "clojure",
                                    "r",
                                    "zig",
                                    "ocaml");

    INFO("language: " << language);

    const auto registry = load_repo_registry();
    const auto* lang = registry.by_name(language);
    REQUIRE(lang != nullptr);

    const auto compile = [&](const std::string& source, const char* label) {
        // Comment-only or blank query files are valid (zero patterns)
        // and ts_query_new accepts them. We still call through to
        // surface any malformed-but-non-empty body.
        if (source.empty()) {
            return;
        }
        uint32_t error_offset = 0;
        TSQueryError error_type = TSQueryErrorNone;
        TSQuery* q = ts_query_new(lang->ts_language,
                                  source.data(),
                                  static_cast<uint32_t>(source.size()),
                                  &error_offset,
                                  &error_type);
        INFO(label << " query for " << language << ": error_type=" << error_type
                   << " offset=" << error_offset);
        REQUIRE(q != nullptr);
        ts_query_delete(q);
    };

    compile(lang->symbols_query_source, "symbols");
    compile(lang->imports_query_source, "imports");
}
