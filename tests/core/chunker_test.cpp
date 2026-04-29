// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/chunker.hpp"

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

using vectra::core::Chunker;
using vectra::core::ChunkKind;
using vectra::core::LanguageRegistry;

namespace {

LanguageRegistry load_repo_registry() {
    const std::filesystem::path repo_root = VECTRA_REPO_ROOT;
    const std::filesystem::path manifest = repo_root / "languages.toml";
    return LanguageRegistry::from_toml(manifest, repo_root);
}

constexpr std::string_view kPythonSource = R"PY(
def add(a, b):
    return a + b


class Greeter:
    def __init__(self, name):
        self.name = name

    def greet(self):
        return f"Hello, {self.name}"
)PY";

constexpr std::string_view kRustSource = R"RUST(
fn add(a: i32, b: i32) -> i32 {
    a + b
}

struct Greeter {
    name: String,
}

impl Greeter {
    fn new(name: &str) -> Self {
        Greeter { name: name.to_string() }
    }
}
)RUST";

}  // namespace

TEST_CASE("chunker emits at least the function and class for a Python source", "[chunker]") {
    auto registry = load_repo_registry();
    Chunker chunker(registry);

    const auto* python = registry.by_name("python");
    REQUIRE(python != nullptr);

    const auto chunks = chunker.chunk(kPythonSource, *python);
    REQUIRE_FALSE(chunks.empty());

    // We expect at least: top-level `add`, top-level `Greeter`,
    // and the two methods inside Greeter.
    const auto count_kind = [&](ChunkKind k) {
        return std::count_if(
            chunks.begin(), chunks.end(), [k](const auto& c) { return c.kind == k; });
    };
    REQUIRE(count_kind(ChunkKind::Function) >= 1);  // add() and methods
    REQUIRE(count_kind(ChunkKind::Class) >= 1);     // Greeter

    const bool has_named_add =
        std::any_of(chunks.begin(), chunks.end(), [](const auto& c) { return c.symbol == "add"; });
    REQUIRE(has_named_add);

    const bool has_named_greeter = std::any_of(
        chunks.begin(), chunks.end(), [](const auto& c) { return c.symbol == "Greeter"; });
    REQUIRE(has_named_greeter);
}

TEST_CASE("chunker assigns a non-zero content hash to each chunk", "[chunker]") {
    auto registry = load_repo_registry();
    Chunker chunker(registry);

    const auto* python = registry.by_name("python");
    const auto chunks = chunker.chunk(kPythonSource, *python);

    for (const auto& c : chunks) {
        // A content hash of all-zeros indicates the hash code did not
        // run; we never expect that on real input.
        const bool all_zero = std::all_of(c.content_hash.bytes.begin(),
                                          c.content_hash.bytes.end(),
                                          [](uint8_t b) { return b == 0; });
        REQUIRE_FALSE(all_zero);
    }
}

TEST_CASE("chunker handles Rust impl blocks and structs", "[chunker]") {
    auto registry = load_repo_registry();
    Chunker chunker(registry);

    const auto* rust = registry.by_name("rust");
    REQUIRE(rust != nullptr);

    const auto chunks = chunker.chunk(kRustSource, *rust);
    REQUIRE_FALSE(chunks.empty());

    const bool has_struct = std::any_of(chunks.begin(), chunks.end(), [](const auto& c) {
        return c.kind == ChunkKind::Class && c.symbol == "Greeter";
    });
    REQUIRE(has_struct);

    const bool has_fn_add = std::any_of(chunks.begin(), chunks.end(), [](const auto& c) {
        return c.kind == ChunkKind::Function && c.symbol == "add";
    });
    REQUIRE(has_fn_add);
}

TEST_CASE("chunker returns empty for empty source", "[chunker]") {
    auto registry = load_repo_registry();
    Chunker chunker(registry);

    const auto* python = registry.by_name("python");
    const auto chunks = chunker.chunk("", *python);
    REQUIRE(chunks.empty());
}

TEST_CASE("chunk_path skips unknown extensions silently", "[chunker]") {
    auto registry = load_repo_registry();
    Chunker chunker(registry);

    REQUIRE(chunker.chunk_path("anything", "unknownextension").empty());
}

TEST_CASE("chunker pulls names out of the C++ declarator chain", "[chunker]") {
    constexpr std::string_view kCppSource = R"CPP(
        namespace foo {

        int free_function(int x) {
            return x + 1;
        }

        class Bar {
        public:
            void inline_method() {}
            int another();
        };

        int Bar::another() {
            return 42;
        }

        }  // namespace foo
    )CPP";

    auto registry = load_repo_registry();
    Chunker chunker(registry);

    const auto* cpp = registry.by_name("cpp");
    REQUIRE(cpp != nullptr);

    const auto chunks = chunker.chunk(kCppSource, *cpp);
    REQUIRE_FALSE(chunks.empty());

    // Free function — name lives behind function_definition.declarator.declarator.
    const bool has_free = std::any_of(chunks.begin(), chunks.end(), [](const auto& c) {
        return c.kind == ChunkKind::Function && c.symbol == "free_function";
    });
    REQUIRE(has_free);

    // Inline member function — same shape, name lands in field_identifier.
    const bool has_inline_method = std::any_of(chunks.begin(), chunks.end(), [](const auto& c) {
        return c.kind == ChunkKind::Method && c.symbol == "inline_method";
    });
    REQUIRE(has_inline_method);

    // Out-of-line member definition — declarator chain ends at
    // qualified_identifier "Bar::another"; we surface the qualified
    // form verbatim because that is the searchable thing.
    const bool has_qualified = std::any_of(chunks.begin(), chunks.end(), [](const auto& c) {
        return c.kind == ChunkKind::Method && c.symbol == "Bar::another";
    });
    REQUIRE(has_qualified);
}
