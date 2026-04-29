// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "claude_subprocess.hpp"

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using vectra::cli::compose_prompt;
using vectra::cli::ContextChunk;
using vectra::cli::PromptComposition;
using vectra::cli::TempFile;

namespace {

[[nodiscard]] ContextChunk make_chunk(
    std::string file, int start, int end, std::string symbol, std::string kind, std::string body) {
    ContextChunk c;
    c.file_path = std::move(file);
    c.start_line = start;
    c.end_line = end;
    c.symbol = std::move(symbol);
    c.kind = std::move(kind);
    c.text = std::move(body);
    return c;
}

}  // namespace

TEST_CASE("compose_prompt emits the task on the first line", "[fix][prompt]") {
    PromptComposition comp;
    comp.task = "rename Foo to Bar";
    const auto out = compose_prompt(comp);
    REQUIRE(out.starts_with("TASK: rename Foo to Bar\n"));
}

TEST_CASE("compose_prompt with no context emits only the task", "[fix][prompt]") {
    PromptComposition comp;
    comp.task = "do nothing";
    const auto out = compose_prompt(comp);
    REQUIRE(out == "TASK: do nothing\n");
    REQUIRE(out.find("<context") == std::string::npos);
}

TEST_CASE("compose_prompt wraps each chunk in a <context> block with metadata", "[fix][prompt]") {
    PromptComposition comp;
    comp.task = "rename greeting";
    comp.context = {
        make_chunk("src/foo.cpp", 42, 58, "greet", "function", "void greet() { ... }\n"),
        make_chunk("src/bar.hpp", 10, 12, "Greeter", "class", "class Greeter { ... };\n"),
    };

    const auto out = compose_prompt(comp);

    REQUIRE(out.find("<context file=\"src/foo.cpp\" lines=\"42-58\" symbol=\"greet\" "
                     "kind=\"function\">") != std::string::npos);
    REQUIRE(out.find("void greet() { ... }") != std::string::npos);
    REQUIRE(out.find("</context>") != std::string::npos);

    REQUIRE(out.find("<context file=\"src/bar.hpp\" lines=\"10-12\" symbol=\"Greeter\" "
                     "kind=\"class\">") != std::string::npos);

    // Both blocks closed; expect at least two </context> markers.
    std::size_t closes = 0;
    std::size_t pos = 0;
    while ((pos = out.find("</context>", pos)) != std::string::npos) {
        ++closes;
        ++pos;
    }
    REQUIRE(closes == 2);
}

TEST_CASE("compose_prompt omits empty symbol and kind attributes", "[fix][prompt]") {
    PromptComposition comp;
    comp.task = "x";
    comp.context = {
        make_chunk("src/a.cpp", 1, 5, "", "", "// anonymous chunk\n"),
    };

    const auto out = compose_prompt(comp);

    REQUIRE(out.find("<context file=\"src/a.cpp\" lines=\"1-5\">") != std::string::npos);
    REQUIRE(out.find("symbol=") == std::string::npos);
    REQUIRE(out.find("kind=") == std::string::npos);
}

TEST_CASE("compose_prompt ensures a newline before the closing tag", "[fix][prompt]") {
    PromptComposition comp;
    comp.task = "x";
    // Chunk text with no trailing newline.
    comp.context = {
        make_chunk("src/a.cpp", 1, 1, "", "", "no_trailing_newline"),
    };

    const auto out = compose_prompt(comp);
    // The line "no_trailing_newline" is followed directly by </context>
    // — make sure the closer sits on its own line.
    REQUIRE(out.find("no_trailing_newline\n</context>") != std::string::npos);
}

TEST_CASE("TempFile creates and writes a file under the system temp dir", "[fix][tempfile]") {
    std::filesystem::path saved;
    {
        TempFile tmp("unit-test");
        tmp.write("hello world\n");
        saved = tmp.path();

        REQUIRE(std::filesystem::exists(saved));

        std::ifstream in(saved, std::ios::binary);
        std::ostringstream buf;
        buf << in.rdbuf();
        REQUIRE(buf.str() == "hello world\n");
    }
    // Destructor removes the file.
    REQUIRE_FALSE(std::filesystem::exists(saved));
}

TEST_CASE("TempFile destructor is safe when the file was never written", "[fix][tempfile]") {
    // Constructor reserves a path but does not create the file. The
    // destructor must not throw when there's nothing to remove.
    REQUIRE_NOTHROW([] {
        TempFile tmp("never-written");
        (void)tmp.path();
    }());
}
