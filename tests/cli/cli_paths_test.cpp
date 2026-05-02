// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "cli_paths.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using vectra::cli::current_exe_path;
using vectra::cli::find_project_root;
using vectra::cli::resolve_adapters_dir;
using vectra::cli::resolve_config_path;

namespace {

namespace fs = std::filesystem;

fs::path make_tmp_root(std::string_view label) {
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    static std::atomic<int> counter{0};
    auto root = fs::temp_directory_path() / "vectra-cli-paths-test" /
                (std::string{label} + "-" + session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(root);
    return root;
}

}  // namespace

TEST_CASE("find_project_root returns the directory containing .vectra", "[cli_paths]") {
    const auto root = make_tmp_root("vectra-marker");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / "src" / "deep" / "nested");

    const auto got = find_project_root(root / "src" / "deep" / "nested");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root falls back to .git when no .vectra is present", "[cli_paths]") {
    const auto root = make_tmp_root("git-marker");
    fs::create_directories(root / ".git");
    fs::create_directories(root / "subdir");

    const auto got = find_project_root(root / "subdir");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root prefers the closest marker on overlapping ancestors", "[cli_paths]") {
    const auto outer = make_tmp_root("outer");
    fs::create_directories(outer / ".git");
    const auto inner = outer / "child";
    fs::create_directories(inner / ".vectra");
    fs::create_directories(inner / "deeper");

    const auto got = find_project_root(inner / "deeper");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, inner));
}

TEST_CASE("find_project_root returns nullopt when no marker exists", "[cli_paths]") {
    const auto root = make_tmp_root("no-marker");
    // No .vectra, no .git anywhere up the tree from this tmp dir.
    fs::create_directories(root / "a" / "b");
    const auto got = find_project_root(root / "a" / "b");
    // The temp dir parent might or might not have a .git; we cannot
    // assume the absence of markers above the temp prefix. So this
    // test only asserts that the result, if present, is at most the
    // tmp root or above — i.e., it never spuriously returns a/b
    // itself.
    if (got) {
        REQUIRE_FALSE(fs::equivalent(*got, root / "a" / "b"));
    }
}

TEST_CASE("resolve_config_path returns the override when set", "[cli_paths]") {
    const auto override_path = fs::path{"/some/elsewhere.toml"};
    const auto got = resolve_config_path(fs::path{"/proj"}, override_path);
    REQUIRE(got == override_path);
}

TEST_CASE("resolve_config_path defaults to <root>/.vectra/config.toml", "[cli_paths]") {
    const auto got = resolve_config_path(fs::path{"/proj"}, fs::path{});
    REQUIRE(got == fs::path{"/proj"} / ".vectra" / "config.toml");
}

TEST_CASE("resolve_adapters_dir prefers the override when it exists", "[cli_paths]") {
    const auto root = make_tmp_root("explicit-adapters");
    const auto custom = root / "custom-adapters";
    fs::create_directories(custom);

    const auto got = resolve_adapters_dir(root, custom);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, custom));
}

TEST_CASE("resolve_adapters_dir returns nullopt when override does not exist", "[cli_paths]") {
    const auto root = make_tmp_root("missing-adapters");
    const auto got = resolve_adapters_dir(root, root / "does-not-exist");
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("resolve_adapters_dir picks up <repo>/adapters when no override", "[cli_paths]") {
    const auto root = make_tmp_root("repo-local-adapters");
    fs::create_directories(root / "adapters");

    const auto got = resolve_adapters_dir(root, fs::path{});
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root / "adapters"));
}

TEST_CASE("current_exe_path returns a real, existing file", "[cli_paths]") {
    const auto p = current_exe_path();
    REQUIRE_FALSE(p.empty());
    std::error_code ec;
    REQUIRE(fs::exists(p, ec));
    REQUIRE(fs::is_regular_file(p, ec));
}

TEST_CASE("current_exe_path returns an absolute path", "[cli_paths]") {
    const auto p = current_exe_path();
    REQUIRE_FALSE(p.empty());
    REQUIRE(p.is_absolute());
}

// ---------------------------------------------------------------------------
// find_project_root edge cases
// ---------------------------------------------------------------------------

TEST_CASE("find_project_root normalizes a trailing separator on the start path", "[cli_paths]") {
    const auto root = make_tmp_root("trailing-sep");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / "child");

    // Force a trailing forward slash; fs::path accepts it on all platforms.
    const auto with_slash = fs::path{(root / "child").string() + "/"};
    const auto got = find_project_root(with_slash);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root resolves dot-segments in the start path", "[cli_paths]") {
    const auto root = make_tmp_root("dot-segments");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / "a" / "b");

    const auto messy = root / "a" / "b" / ".." / "." / "b";
    const auto got = find_project_root(messy);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root identifies a root that holds both .vectra and .git", "[cli_paths]") {
    const auto root = make_tmp_root("both-markers");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / ".git");
    fs::create_directories(root / "deep");

    const auto got = find_project_root(root / "deep");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root accepts a .git regular file (submodule worktree)", "[cli_paths]") {
    // Submodule worktrees use a `.git` *file* containing `gitdir: ...` instead
    // of a directory. has_marker uses fs::exists, which matches both.
    const auto root = make_tmp_root("git-as-file");
    {
        std::ofstream out{root / ".git"};
        out << "gitdir: ../.git/modules/sub";
    }
    fs::create_directories(root / "src");

    const auto got = find_project_root(root / "src");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root walks past non-existent leaf segments", "[cli_paths]") {
    const auto root = make_tmp_root("ghost-leaves");
    fs::create_directories(root / ".vectra");

    // weakly_canonical normalizes paths whose tail does not yet exist.
    const auto ghost = root / "does" / "not" / "exist" / "yet";
    const auto got = find_project_root(ghost);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root terminates at the filesystem root without looping", "[cli_paths]") {
#ifdef _WIN32
    const auto drive = fs::temp_directory_path().root_path();
    const auto got = find_project_root(drive);
#else
    const auto got = find_project_root(fs::path{"/"});
#endif
    // Reaching this line is the actual assertion: the call returned at all.
    // The result itself depends on whether the host has a marker at the root.
    (void)got;
    SUCCEED("traversal terminated at filesystem root");
}

TEST_CASE("find_project_root tolerates an empty start path", "[cli_paths]") {
    const auto got = find_project_root(fs::path{});
    (void)got;
    SUCCEED("did not crash on an empty input path");
}

TEST_CASE("find_project_root resolves a symlink in the start path", "[cli_paths]") {
    const auto root = make_tmp_root("symlink-start");
    fs::create_directories(root / ".vectra");
    fs::create_directories(root / "real" / "deep");

    const auto link = root / "link";
    std::error_code ec;
    fs::create_directory_symlink(root / "real", link, ec);
    if (ec) {
        SKIP("cannot create directory symlinks here: " << ec.message());
    }

    const auto got = find_project_root(link / "deep");
    REQUIRE(got.has_value());
    // weakly_canonical resolves through the symlink, so we land on the same
    // inode as `root` regardless of which spelling was used to enter.
    REQUIRE(fs::equivalent(*got, root));
}

TEST_CASE("find_project_root recognizes a .vectra marker that is itself a symlink", "[cli_paths]") {
    const auto root = make_tmp_root("symlink-marker");
    const auto target = make_tmp_root("symlink-marker-target");
    fs::create_directories(target / "anything");
    fs::create_directories(root / "child");

    std::error_code ec;
    fs::create_directory_symlink(target, root / ".vectra", ec);
    if (ec) {
        SKIP("cannot create directory symlinks here: " << ec.message());
    }

    const auto got = find_project_root(root / "child");
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}

// ---------------------------------------------------------------------------
// resolve_config_path / resolve_adapters_dir edge cases
// ---------------------------------------------------------------------------

TEST_CASE("resolve_config_path passes the override through unchanged", "[cli_paths]") {
    const auto override_path = fs::path{"/elsewhere/config.toml"};
    REQUIRE(resolve_config_path(fs::path{}, override_path) == override_path);
    REQUIRE(resolve_config_path(fs::path{"/proj"}, override_path) == override_path);
    REQUIRE(resolve_config_path(fs::path{"/another/root"}, override_path) == override_path);
}

TEST_CASE("resolve_config_path with an empty repo_root yields a relative default", "[cli_paths]") {
    const auto got = resolve_config_path(fs::path{}, fs::path{});
    REQUIRE(got == fs::path{".vectra"} / "config.toml");
}

TEST_CASE("resolve_adapters_dir rejects an override that points at a regular file", "[cli_paths]") {
    const auto root = make_tmp_root("file-override");
    const auto file = root / "not-a-dir";
    {
        std::ofstream out{file};
        out << "marker";
    }

    const auto got = resolve_adapters_dir(root, file);
    REQUIRE_FALSE(got.has_value());
}

TEST_CASE("resolve_adapters_dir returns an absolute, normalized path", "[cli_paths]") {
    const auto root = make_tmp_root("normalized-adapters");
    fs::create_directories(root / "adapters");

    const auto got = resolve_adapters_dir(root, fs::path{});
    REQUIRE(got.has_value());
    REQUIRE(got->is_absolute());
    for (const auto& part : *got) {
        REQUIRE(part != "..");
        REQUIRE(part != ".");
    }
}

// ---------------------------------------------------------------------------
// Platform-specific edge cases
// ---------------------------------------------------------------------------

#ifdef _WIN32
TEST_CASE("find_project_root handles Windows paths beyond the legacy MAX_PATH limit",
          "[cli_paths][windows]") {
    const auto root = make_tmp_root("long-path");
    fs::create_directories(root / ".vectra");

    // Build a chain whose total length exceeds the legacy 260-character cap.
    // Requires LongPathsEnabled in the registry, which the GitHub
    // windows-2022 runners ship with on by default.
    fs::path deep = root;
    for (int i = 0; i < 16; ++i) {
        deep /= "segment-with-padding-to-break-260-char-limit";
    }
    REQUIRE(deep.string().size() > 260);

    std::error_code ec;
    fs::create_directories(deep, ec);
    if (ec) {
        SKIP("could not create a long path on this filesystem: " << ec.message());
    }

    const auto got = find_project_root(deep);
    REQUIRE(got.has_value());
    REQUIRE(fs::equivalent(*got, root));
}
#endif

#ifdef __APPLE__
TEST_CASE("find_project_root behavior on case-insensitive APFS volumes", "[cli_paths][apple]") {
    const auto root = make_tmp_root("case-marker");
    // Capital V — not the canonical name has_marker probes for.
    fs::create_directories(root / ".Vectra");
    fs::create_directories(root / "child");

    // Probe filesystem case-sensitivity by asking for the lowercase spelling.
    std::error_code ec;
    const bool case_insensitive = fs::exists(root / ".vectra", ec) && !ec;

    const auto got = find_project_root(root / "child");
    if (case_insensitive) {
        // Default APFS volumes are case-insensitive: `.vectra` resolves to
        // `.Vectra`, so the marker is found.
        REQUIRE(got.has_value());
        REQUIRE(fs::equivalent(*got, root));
    } else {
        // Case-sensitive APFS variant: `.vectra` does not match `.Vectra`,
        // so this `root` cannot be the project root reported back.
        if (got) {
            REQUIRE_FALSE(fs::equivalent(*got, root));
        }
    }
}
#endif
