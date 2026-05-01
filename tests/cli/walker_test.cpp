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
