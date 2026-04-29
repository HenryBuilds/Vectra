// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/diff.hpp"

#include <catch2/catch_test_macros.hpp>
#include <string>

using vectra::exec::parse_unified_diff;
using vectra::exec::Patch;

TEST_CASE("parser returns empty Patch for prose-only input", "[diff]") {
    const auto patch = parse_unified_diff("Just some explanation, no diff.");
    REQUIRE(patch.empty());
}

TEST_CASE("parser handles a minimal single-hunk modify", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- a/src/foo.cpp
+++ b/src/foo.cpp
@@ -1,3 +1,3 @@
 line one
-old line two
+new line two
 line three
)DIFF";

    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);

    const auto& f = patch.files[0];
    REQUIRE(f.old_path == "src/foo.cpp");
    REQUIRE(f.new_path == "src/foo.cpp");
    REQUIRE_FALSE(f.is_new_file);
    REQUIRE_FALSE(f.is_deleted);
    REQUIRE(f.hunks.size() == 1);

    const auto& h = f.hunks[0];
    REQUIRE(h.old_start == 1);
    REQUIRE(h.old_count == 3);
    REQUIRE(h.new_start == 1);
    REQUIRE(h.new_count == 3);
    REQUIRE(h.lines.size() == 4);
    REQUIRE(h.lines[1] == "-old line two");
    REQUIRE(h.lines[2] == "+new line two");
}

TEST_CASE("parser handles new file via /dev/null on the old side", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- /dev/null
+++ b/src/new.cpp
@@ -0,0 +1,2 @@
+first
+second
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].is_new_file);
    REQUIRE(patch.files[0].old_path.empty());
    REQUIRE(patch.files[0].new_path == "src/new.cpp");
    REQUIRE(patch.files[0].hunks[0].lines.size() == 2);
}

TEST_CASE("parser handles deleted file via /dev/null on the new side", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- a/src/old.cpp
+++ /dev/null
@@ -1,2 +0,0 @@
-line one
-line two
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].is_deleted);
    REQUIRE(patch.files[0].old_path == "src/old.cpp");
    REQUIRE(patch.files[0].new_path.empty());
}

TEST_CASE("parser handles default count of 1 in hunk header", "[diff]") {
    // "@@ -10 +10 @@" without ",count" means count=1.
    constexpr std::string_view text = R"DIFF(--- a/x
+++ b/x
@@ -10 +10 @@
-old
+new
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].hunks[0].old_count == 1);
    REQUIRE(patch.files[0].hunks[0].new_count == 1);
}

TEST_CASE("parser handles multiple hunks per file", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- a/foo
+++ b/foo
@@ -1,2 +1,2 @@
-a
+A
 b
@@ -10,2 +10,2 @@
 c
-d
+D
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].hunks.size() == 2);
    REQUIRE(patch.files[0].hunks[0].old_start == 1);
    REQUIRE(patch.files[0].hunks[1].old_start == 10);
}

TEST_CASE("parser handles multiple files in one response", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- a/foo
+++ b/foo
@@ -1 +1 @@
-old foo
+new foo
--- a/bar
+++ b/bar
@@ -1 +1 @@
-old bar
+new bar
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 2);
    REQUIRE(patch.files[0].new_path == "foo");
    REQUIRE(patch.files[1].new_path == "bar");
}

TEST_CASE("parser tolerates surrounding prose", "[diff]") {
    constexpr std::string_view text = R"DIFF(Sure, here is the patch:

--- a/foo
+++ b/foo
@@ -1 +1 @@
-old
+new

That should do it.
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].new_path == "foo");
}

TEST_CASE("parser strips a/ b/ prefixes only when paths begin with them", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- src/a.cpp
+++ src/a.cpp
@@ -1 +1 @@
-old
+new
)DIFF";
    const auto patch = parse_unified_diff(text);
    REQUIRE(patch.files.size() == 1);
    REQUIRE(patch.files[0].old_path == "src/a.cpp");
    REQUIRE(patch.files[0].new_path == "src/a.cpp");
}

TEST_CASE("parser rejects combined / 3-way diffs", "[diff]") {
    constexpr std::string_view text = R"DIFF(--- a/x
+++ b/x
@@@ -1,1 -1,1 +1,1 @@@
- old
+ new
)DIFF";
    REQUIRE_THROWS(parse_unified_diff(text));
}

TEST_CASE("parser rejects binary-file markers loudly", "[diff]") {
    constexpr std::string_view text = "Binary files a/img.png and b/img.png differ\n";
    REQUIRE_THROWS(parse_unified_diff(text));
}

TEST_CASE("parser raises on truncated hunk body", "[diff]") {
    // Header claims 3 old / 3 new but only 1 line follows.
    constexpr std::string_view text = R"DIFF(--- a/x
+++ b/x
@@ -1,3 +1,3 @@
 only one
)DIFF";
    REQUIRE_THROWS(parse_unified_diff(text));
}
