// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "walker.hpp"

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "vectra/core/language.hpp"

using vectra::cli::FileWalker;
using vectra::core::LanguageRegistry;

namespace {

namespace fs = std::filesystem;

// Pull in the same registry the CLI uses at run time. The tests
// exercise the walker against the project's actual languages.toml
// rather than mocking it; this catches regressions where a query
// path silently moves and the walker stops finding files.
LanguageRegistry load_repo_registry() {
    const fs::path repo_root = VECTRA_REPO_ROOT;
    const fs::path manifest = repo_root / "languages.toml";
    return LanguageRegistry::from_toml(manifest, repo_root);
}

// Build a unique temporary directory tree under the OS temp dir.
// Each test gets its own subtree so runs do not collide.
fs::path make_tmp_root() {
    static std::atomic<int> counter{0};
    static const auto session =
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());

    const fs::path base = fs::temp_directory_path() / "vectra-walker-test" /
                          (session + "-" + std::to_string(counter.fetch_add(1)));
    fs::create_directories(base);
    return base;
}

void write_file(const fs::path& path, std::string_view contents) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

bool contains(const std::vector<fs::path>& v, const fs::path& needle) {
    for (const auto& p : v) {
        if (p == needle)
            return true;
    }
    return false;
}

// Run a shell command silently and return its exit code. Used to
// drive `git init` from within the gitignore test below.
int run_silently(const std::string& cmd) {
#ifdef _WIN32
    const std::string suffix = " >NUL 2>&1";
#else
    const std::string suffix = " >/dev/null 2>&1";
#endif
    return std::system((cmd + suffix).c_str());
}

bool git_available() {
    return run_silently("git --version") == 0;
}

}  // namespace

TEST_CASE("walker yields registered source files and skips others", "[walker]") {
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    const auto py = root / "src" / "main.py";
    const auto rs = root / "src" / "lib.rs";
    const auto txt = root / "README.txt";  // unregistered → skipped
    write_file(py, "def main(): pass\n");
    write_file(rs, "fn main() {}\n");
    write_file(txt, "hello\n");

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(found.size() == 2);
    REQUIRE(contains(found, py));
    REQUIRE(contains(found, rs));
    REQUIRE_FALSE(contains(found, txt));
}

TEST_CASE("walker prunes well-known build / VCS directories", "[walker]") {
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    const auto kept = root / "src" / "good.py";
    const auto skipped = root / "node_modules" / "lodash" / "index.js";
    const auto vcs = root / ".git" / "config.py";  // .py inside .git
    const auto build = root / "target" / "debug" / "release.rs";

    write_file(kept, "def main(): pass\n");
    write_file(skipped, "module.exports = {};\n");
    write_file(vcs, "# inside .git\n");
    write_file(build, "fn target() {}\n");

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, kept));
    REQUIRE_FALSE(contains(found, skipped));
    REQUIRE_FALSE(contains(found, vcs));
    REQUIRE_FALSE(contains(found, build));
}

TEST_CASE("walker drops files that exceed the size limit", "[walker]") {
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    const auto big = root / "huge.py";
    const auto small = root / "small.py";
    write_file(big, std::string(2 * 1024, 'x'));  // 2 KB
    write_file(small, "pass\n");

    FileWalker::Options opts;
    opts.max_file_size_bytes = 1 * 1024;  // 1 KB cap
    const auto found = FileWalker{opts}.walk(root, registry);

    REQUIRE(contains(found, small));
    REQUIRE_FALSE(contains(found, big));
}

TEST_CASE("walker output is sorted for deterministic downstream ordering", "[walker]") {
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    write_file(root / "z.py", "pass\n");
    write_file(root / "a.py", "pass\n");
    write_file(root / "m.py", "pass\n");

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(found.size() == 3);
    for (std::size_t i = 1; i < found.size(); ++i) {
        REQUIRE(found[i - 1] <= found[i]);
    }
}

TEST_CASE("walker on a non-existent path returns empty without throwing", "[walker]") {
    const auto registry = load_repo_registry();
    const auto found = FileWalker{}.walk("/path/that/does/not/exist", registry);
    REQUIRE(found.empty());
}

TEST_CASE("walker honours .gitignore inside a git repository", "[walker][git]") {
    if (!git_available()) {
        SUCCEED("git not on PATH; skipping gitignore-aware walk test");
        return;
    }

    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    // Layout:
    //   src/kept.py        — ordinary source, must show up
    //   gen/ignored.py     — listed in .gitignore, must drop
    //   private/secret.py  — listed in .gitignore, must drop
    //   .gitignore         — declares the ignores (a regular file)
    write_file(root / "src" / "kept.py", "def main(): pass\n");
    write_file(root / "gen" / "ignored.py", "pass\n");
    write_file(root / "private" / "secret.py", "pass\n");
    write_file(root / ".gitignore", "gen/\nprivate/\n");

    const std::string init_cmd = "git -C \"" + root.string() + "\" init -q";
    REQUIRE(run_silently(init_cmd) == 0);

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / "src" / "kept.py"));
    REQUIRE_FALSE(contains(found, root / "gen" / "ignored.py"));
    REQUIRE_FALSE(contains(found, root / "private" / "secret.py"));
}

TEST_CASE("walker honours glob patterns in .gitignore", "[walker][git]") {
    if (!git_available()) {
        SUCCEED("git not on PATH; skipping glob test");
        return;
    }

    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    // Glob *.tmp.py in the entire tree should match files at any
    // depth, not just at the repo root.
    write_file(root / "src" / "kept.py", "def main(): pass\n");
    write_file(root / "src" / "scratch.tmp.py", "pass\n");
    write_file(root / "deep" / "nested" / "throwaway.tmp.py", "pass\n");
    write_file(root / ".gitignore", "*.tmp.py\n");

    const std::string init_cmd = "git -C \"" + root.string() + "\" init -q";
    REQUIRE(run_silently(init_cmd) == 0);

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / "src" / "kept.py"));
    REQUIRE_FALSE(contains(found, root / "src" / "scratch.tmp.py"));
    REQUIRE_FALSE(contains(found, root / "deep" / "nested" / "throwaway.tmp.py"));
}

TEST_CASE("walker respects .gitignore negation patterns", "[walker][git]") {
    if (!git_available()) {
        SUCCEED("git not on PATH; skipping negation test");
        return;
    }

    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    // Ignore everything in build/, then re-include build/keep.py
    // via the leading '!' negation.
    write_file(root / "build" / "drop1.py", "pass\n");
    write_file(root / "build" / "drop2.py", "pass\n");
    write_file(root / "build" / "keep.py", "pass\n");
    write_file(root / ".gitignore", "build/*\n!build/keep.py\n");

    const std::string init_cmd = "git -C \"" + root.string() + "\" init -q";
    REQUIRE(run_silently(init_cmd) == 0);

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / "build" / "keep.py"));
    REQUIRE_FALSE(contains(found, root / "build" / "drop1.py"));
    REQUIRE_FALSE(contains(found, root / "build" / "drop2.py"));
}

TEST_CASE("walker honours nested .gitignore at deeper directory levels", "[walker][git]") {
    if (!git_available()) {
        SUCCEED("git not on PATH; skipping nested-gitignore test");
        return;
    }

    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    // Top-level .gitignore is empty (just to make this a real
    // git-aware tree); the nested module/.gitignore is the one
    // that drops module/secret.py.
    write_file(root / "module" / "kept.py", "def x(): pass\n");
    write_file(root / "module" / "secret.py", "pass\n");
    write_file(root / "module" / ".gitignore", "secret.py\n");
    write_file(root / ".gitignore", "");

    const std::string init_cmd = "git -C \"" + root.string() + "\" init -q";
    REQUIRE(run_silently(init_cmd) == 0);

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / "module" / "kept.py"));
    REQUIRE_FALSE(contains(found, root / "module" / "secret.py"));
}

TEST_CASE("walker keeps .git directory itself out of the file set", "[walker][git]") {
    if (!git_available()) {
        SUCCEED("git not on PATH; skipping .git filter test");
        return;
    }

    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    write_file(root / "src" / "kept.py", "def main(): pass\n");
    const std::string init_cmd = "git -C \"" + root.string() + "\" init -q";
    REQUIRE(run_silently(init_cmd) == 0);

    // git ls-files never lists files under .git/ in the first
    // place, so we just need to assert the walker output is clean.
    // This is the safety property: even if a .py snuck inside .git
    // somehow (config templates, hooks, …) the walker must skip it.
    const auto found = FileWalker{}.walk(root, registry);
    for (const auto& p : found) {
        const auto rel = std::filesystem::relative(p, root).generic_string();
        REQUIRE(rel.find(".git/") != 0);
        REQUIRE(rel != ".git");
    }
}

TEST_CASE("walker fallback (non-git) still drops universal-skip directories",
          "[walker][fallback]") {
    // This exercises the path triggered when try_git_ls_files
    // fails — typically because the root is not inside a working
    // tree. make_tmp_root never runs `git init`, so the walker
    // falls through to the recursive_directory_iterator branch.
    // We assert the small hardcoded skip list still kicks in.
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    write_file(root / "src" / "kept.py", "def main(): pass\n");
    write_file(root / "node_modules" / "lodash" / "index.js", "module.exports={};\n");
    write_file(root / "__pycache__" / "cached.py", "# bytecode\n");
    write_file(root / "target" / "debug" / "junk.rs", "fn x() {}\n");
    write_file(root / "build" / "out.cpp", "int main() {}\n");
    write_file(root / ".vectra" / "secret.py", "# vectra state\n");

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / "src" / "kept.py"));
    REQUIRE_FALSE(contains(found, root / "node_modules" / "lodash" / "index.js"));
    REQUIRE_FALSE(contains(found, root / "__pycache__" / "cached.py"));
    REQUIRE_FALSE(contains(found, root / "target" / "debug" / "junk.rs"));
    REQUIRE_FALSE(contains(found, root / "build" / "out.cpp"));
    REQUIRE_FALSE(contains(found, root / ".vectra" / "secret.py"));
}

TEST_CASE("walker fallback no longer drops framework-specific dirs (.next etc)",
          "[walker][fallback]") {
    // The fallback skip list intentionally shrunk: framework
    // build dirs (.next, .turbo, .svelte-kit, .astro) are no
    // longer in it because every modern project's .gitignore
    // covers them. Outside a git repo the walker shows them —
    // that's acceptable because the user is in a non-tracked
    // ad-hoc directory and we have no signal otherwise.
    const auto registry = load_repo_registry();
    const auto root = make_tmp_root();

    write_file(root / ".next" / "page.js", "module.exports={};\n");

    const auto found = FileWalker{}.walk(root, registry);
    REQUIRE(contains(found, root / ".next" / "page.js"));
}
