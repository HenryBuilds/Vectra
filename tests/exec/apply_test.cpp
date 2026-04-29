// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/apply.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "vectra/exec/diff.hpp"

using vectra::exec::apply_patch;
using vectra::exec::ApplyOptions;
using vectra::exec::parse_unified_diff;
using vectra::exec::rollback;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_repo() {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-exec-test" /
                (session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

void write_file(const fs::path& p, std::string_view contents) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

}  // namespace

TEST_CASE("apply_patch modifies a file in place", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "src" / "foo.cpp", "line one\nold line two\nline three\n");

    constexpr std::string_view diff = R"DIFF(--- a/src/foo.cpp
+++ b/src/foo.cpp
@@ -1,3 +1,3 @@
 line one
-old line two
+new line two
 line three
)DIFF";

    const auto patch = parse_unified_diff(diff);
    ApplyOptions opts;
    opts.repo_root = repo;
    const auto result = apply_patch(patch, opts);

    REQUIRE(result.files_modified.size() == 1);
    REQUIRE(result.files_created.empty());
    REQUIRE(result.files_deleted.empty());

    const std::string body = read_file(repo / "src" / "foo.cpp");
    REQUIRE(body == "line one\nnew line two\nline three\n");
}

TEST_CASE("apply_patch creates a new file", "[apply]") {
    const auto repo = make_tmp_repo();

    constexpr std::string_view diff = R"DIFF(--- /dev/null
+++ b/src/new.cpp
@@ -0,0 +1,2 @@
+hello
+world
)DIFF";

    const auto patch = parse_unified_diff(diff);
    ApplyOptions opts;
    opts.repo_root = repo;
    const auto result = apply_patch(patch, opts);

    REQUIRE(result.files_created.size() == 1);
    REQUIRE(read_file(repo / "src" / "new.cpp") == "hello\nworld\n");
}

TEST_CASE("apply_patch deletes a file", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "gone.cpp", "doomed\n");

    constexpr std::string_view diff = R"DIFF(--- a/gone.cpp
+++ /dev/null
@@ -1 +0,0 @@
-doomed
)DIFF";

    const auto patch = parse_unified_diff(diff);
    ApplyOptions opts;
    opts.repo_root = repo;
    const auto result = apply_patch(patch, opts);

    REQUIRE(result.files_deleted.size() == 1);
    REQUIRE_FALSE(fs::exists(repo / "gone.cpp"));
}

TEST_CASE("apply_patch raises on context mismatch and leaves the tree untouched", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "src" / "foo.cpp", "line one\nDIFFERENT\nline three\n");

    constexpr std::string_view diff = R"DIFF(--- a/src/foo.cpp
+++ b/src/foo.cpp
@@ -1,3 +1,3 @@
 line one
-old line two
+new line two
 line three
)DIFF";

    const auto patch = parse_unified_diff(diff);
    ApplyOptions opts;
    opts.repo_root = repo;

    REQUIRE_THROWS(apply_patch(patch, opts));
    // Original file content is preserved.
    REQUIRE(read_file(repo / "src" / "foo.cpp") == "line one\nDIFFERENT\nline three\n");
}

TEST_CASE("dry_run reports actions without touching the working tree", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "x.cpp", "old\n");

    constexpr std::string_view diff = R"DIFF(--- a/x.cpp
+++ b/x.cpp
@@ -1 +1 @@
-old
+new
)DIFF";

    ApplyOptions opts;
    opts.repo_root = repo;
    opts.dry_run = true;

    const auto patch = parse_unified_diff(diff);
    const auto result = apply_patch(patch, opts);

    REQUIRE(result.files_modified.size() == 1);
    REQUIRE(read_file(repo / "x.cpp") == "old\n");
}

TEST_CASE("rollback restores modified files and removes created ones", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "modify.cpp", "old\n");

    constexpr std::string_view diff = R"DIFF(--- a/modify.cpp
+++ b/modify.cpp
@@ -1 +1 @@
-old
+new
--- /dev/null
+++ b/created.cpp
@@ -0,0 +1 @@
+brand new
)DIFF";

    ApplyOptions opts;
    opts.repo_root = repo;

    const auto patch = parse_unified_diff(diff);
    const auto result = apply_patch(patch, opts);

    // Verify the apply actually changed things.
    REQUIRE(read_file(repo / "modify.cpp") == "new\n");
    REQUIRE(fs::exists(repo / "created.cpp"));

    rollback(result.backup_dir);

    // After rollback we are back to the pre-apply state.
    REQUIRE(read_file(repo / "modify.cpp") == "old\n");
    REQUIRE_FALSE(fs::exists(repo / "created.cpp"));
}

TEST_CASE("apply_patch handles multiple hunks per file", "[apply]") {
    const auto repo = make_tmp_repo();
    write_file(repo / "f.cpp", "a1\na2\na3\na4\na5\nb1\nb2\nb3\nb4\nb5\nc1\nc2\nc3\n");

    constexpr std::string_view diff = R"DIFF(--- a/f.cpp
+++ b/f.cpp
@@ -1,3 +1,3 @@
-a1
+A1
 a2
 a3
@@ -11,3 +11,3 @@
 c1
-c2
+C2
 c3
)DIFF";

    ApplyOptions opts;
    opts.repo_root = repo;
    [[maybe_unused]] const auto result = apply_patch(parse_unified_diff(diff), opts);

    REQUIRE(read_file(repo / "f.cpp") == "A1\na2\na3\na4\na5\nb1\nb2\nb3\nb4\nb5\nc1\nC2\nc3\n");
}
