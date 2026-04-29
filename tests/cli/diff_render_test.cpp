// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "diff_render.hpp"

#include <catch2/catch_test_macros.hpp>
#include <sstream>
#include <string>

using vectra::cli::DiffRenderOptions;
using vectra::cli::render_diff;
using vectra::exec::FileDiff;
using vectra::exec::Hunk;
using vectra::exec::Patch;

namespace {

[[nodiscard]] Patch make_modify_patch() {
    Patch p;
    FileDiff f;
    f.old_path = "src/foo.cpp";
    f.new_path = "src/foo.cpp";
    Hunk h;
    h.old_start = 1;
    h.old_count = 3;
    h.new_start = 1;
    h.new_count = 3;
    h.lines = {" line one", "-old line two", "+new line two", " line three"};
    f.hunks.push_back(h);
    p.files.push_back(f);
    return p;
}

}  // namespace

TEST_CASE("render_diff emits headers and bodies without color when disabled", "[diff_render]") {
    std::ostringstream out;
    render_diff(out, make_modify_patch(), DiffRenderOptions{/*use_color=*/false});

    const auto s = out.str();
    REQUIRE(s.find("--- a/src/foo.cpp") != std::string::npos);
    REQUIRE(s.find("+++ b/src/foo.cpp") != std::string::npos);
    REQUIRE(s.find("@@ -1,3 +1,3 @@") != std::string::npos);
    REQUIRE(s.find("-old line two") != std::string::npos);
    REQUIRE(s.find("+new line two") != std::string::npos);
    REQUIRE(s.find(" line one") != std::string::npos);

    // No ANSI escape codes in plain mode.
    REQUIRE(s.find("\x1b[") == std::string::npos);
}

TEST_CASE("render_diff colors add and remove lines when enabled", "[diff_render]") {
    std::ostringstream out;
    render_diff(out, make_modify_patch(), DiffRenderOptions{/*use_color=*/true});

    const auto s = out.str();
    // Green for adds, red for removes, reset after each.
    REQUIRE(s.find("\x1b[32m+new line two\x1b[0m") != std::string::npos);
    REQUIRE(s.find("\x1b[31m-old line two\x1b[0m") != std::string::npos);
    // Context lines remain uncolored.
    REQUIRE(s.find("\x1b[32m line one") == std::string::npos);
    REQUIRE(s.find("\x1b[31m line one") == std::string::npos);
}

TEST_CASE("render_diff prints /dev/null on the old side for new files", "[diff_render]") {
    Patch p;
    FileDiff f;
    f.new_path = "src/added.cpp";
    f.is_new_file = true;
    Hunk h;
    h.old_start = 0;
    h.old_count = 0;
    h.new_start = 1;
    h.new_count = 1;
    h.lines = {"+hello"};
    f.hunks.push_back(h);
    p.files.push_back(f);

    std::ostringstream out;
    render_diff(out, p, DiffRenderOptions{/*use_color=*/false});
    const auto s = out.str();
    REQUIRE(s.find("--- a//dev/null") != std::string::npos);
    REQUIRE(s.find("+++ b/src/added.cpp") != std::string::npos);
}

TEST_CASE("render_diff prints /dev/null on the new side for deleted files", "[diff_render]") {
    Patch p;
    FileDiff f;
    f.old_path = "src/gone.cpp";
    f.is_deleted = true;
    Hunk h;
    h.old_start = 1;
    h.old_count = 1;
    h.new_start = 0;
    h.new_count = 0;
    h.lines = {"-bye"};
    f.hunks.push_back(h);
    p.files.push_back(f);

    std::ostringstream out;
    render_diff(out, p, DiffRenderOptions{/*use_color=*/false});
    const auto s = out.str();
    REQUIRE(s.find("--- a/src/gone.cpp") != std::string::npos);
    REQUIRE(s.find("+++ b//dev/null") != std::string::npos);
}

TEST_CASE("render_diff handles an empty patch silently", "[diff_render]") {
    std::ostringstream out;
    render_diff(out, Patch{}, DiffRenderOptions{/*use_color=*/false});
    REQUIRE(out.str().empty());
}
