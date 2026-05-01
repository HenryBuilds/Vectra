// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "walker.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "vectra/core/language.hpp"

#ifdef _WIN32
#define VECTRA_POPEN _popen
#define VECTRA_PCLOSE _pclose
#else
#include <sys/wait.h>
#define VECTRA_POPEN popen
#define VECTRA_PCLOSE pclose
#endif

namespace vectra::cli {

namespace {

namespace fs = std::filesystem;

[[nodiscard]] std::string shell_quote(std::string_view s) {
    // We feed the result through cmd.exe / /bin/sh via popen, which
    // tokenize on whitespace; wrap whenever the path contains any.
    // Vectra's expected input domain (project roots) does not
    // contain double-quotes, so we deliberately do not escape them
    // here — keeping this in lockstep with the same helper used in
    // claude_subprocess.cpp.
    if (s.find_first_of(" \t") == std::string_view::npos) {
        return std::string{s};
    }
    std::string out;
    out.reserve(s.size() + 2);
    out += '"';
    out += s;
    out += '"';
    return out;
}

[[nodiscard]] int normalize_status(int popen_status) noexcept {
#ifdef _WIN32
    return popen_status;
#else
    if (WIFEXITED(popen_status)) {
        return WEXITSTATUS(popen_status);
    }
    return -1;
#endif
}

// Run `git ls-files` rooted at `root` and return the file set git
// considers part of the working tree (tracked + untracked, ignored
// dropped). Returns nullopt if git is not on PATH, root is not
// inside a working tree, or the subprocess fails for any other
// reason — caller falls back to the filesystem iterator.
//
// We pass:
//   -C <root>                 run as if from <root>
//   -z                        NUL-separated output (handles paths
//                             containing newlines, which is exotic
//                             but happens; \n in JS file names from
//                             dependency repos has been observed)
//   --cached                  files tracked in the index
//   --others                  files in the work tree but not tracked
//   --exclude-standard        apply .gitignore + .git/info/exclude
//                             + global ignore. Without this, all
//                             ignored files would be listed.
//   -- .                      explicit pathspec; without it, git
//                             returns the *entire* working tree
//                             even when -C is a subdirectory.
[[nodiscard]] std::optional<std::vector<fs::path>> try_git_ls_files(const fs::path& root) {
    std::string cmd = "git -C ";
    cmd += shell_quote(root.string());
    cmd += " ls-files -z --cached --others --exclude-standard -- .";
#ifdef _WIN32
    cmd += " 2>NUL";
#else
    cmd += " 2>/dev/null";
#endif

    FILE* pipe = VECTRA_POPEN(cmd.c_str(), "r");
    if (pipe == nullptr) {
        return std::nullopt;
    }

    std::string buf;
    char chunk[4096];
    while (true) {
        const auto n = std::fread(chunk, 1, sizeof(chunk), pipe);
        if (n == 0) {
            break;
        }
        buf.append(chunk, n);
    }

    const int exit_code = normalize_status(VECTRA_PCLOSE(pipe));
    if (exit_code != 0) {
        // Either git is missing, root is outside a repo, or git
        // hit some other error. Either way, fall back.
        return std::nullopt;
    }

    std::vector<fs::path> paths;
    std::size_t start = 0;
    for (std::size_t i = 0; i < buf.size(); ++i) {
        if (buf[i] == '\0') {
            if (i > start) {
                std::string rel(buf.data() + start, i - start);
                paths.push_back(root / fs::path(rel));
            }
            start = i + 1;
        }
    }
    return paths;
}

// Filter common to both the git-driven and filesystem-driven paths:
// drop files that exceed the size limit or whose extension is not
// registered in the language registry.
[[nodiscard]] bool keep_file(const fs::path& path,
                             const core::LanguageRegistry& registry,
                             std::size_t max_file_size_bytes) {
    if (registry.for_path(path) == nullptr) {
        return false;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    const auto size = fs::file_size(path, ec);
    if (ec) {
        return false;
    }
    if (size > max_file_size_bytes) {
        return false;
    }
    return true;
}

// Filesystem fallback: recursive_directory_iterator with the
// hardcoded ignore_dirs. Used when git is unavailable or the root
// is not inside a working tree.
[[nodiscard]] std::vector<fs::path> walk_filesystem(const fs::path& root,
                                                    const FileWalker::Options& opts,
                                                    const core::LanguageRegistry& registry) {
    std::vector<fs::path> out;

    fs::directory_options dopts = fs::directory_options::skip_permission_denied;
    if (opts.follow_symlinks) {
        dopts |= fs::directory_options::follow_directory_symlink;
    }

    std::error_code ec;
    fs::recursive_directory_iterator it(root, dopts, ec);
    if (ec) {
        return out;
    }
    const fs::recursive_directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) {
            // A single bad entry should not abort the walk.
            ec.clear();
            continue;
        }
        const fs::directory_entry& entry = *it;
        const fs::path& path = entry.path();
        const std::string name = path.filename().string();

        if (entry.is_directory(ec)) {
            if (opts.ignore_dirs.contains(name)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        if (!keep_file(path, registry, opts.max_file_size_bytes)) {
            continue;
        }
        out.push_back(path);
    }
    return out;
}

}  // namespace

FileWalker::FileWalker() noexcept : FileWalker(Options{}) {}

FileWalker::FileWalker(Options opts) noexcept : opts_(std::move(opts)) {}

std::vector<std::filesystem::path> FileWalker::walk(const std::filesystem::path& root,
                                                    const core::LanguageRegistry& registry) const {
    namespace fs = std::filesystem;

    std::vector<fs::path> out;
    if (!fs::exists(root)) {
        return out;
    }

    // Preferred path: ask git for the file set. This honours the
    // project's .gitignore (and every other ignore source git
    // tracks) without us having to parse any patterns.
    if (auto from_git = try_git_ls_files(root)) {
        out.reserve(from_git->size());
        for (auto& p : *from_git) {
            if (keep_file(p, registry, opts_.max_file_size_bytes)) {
                out.push_back(std::move(p));
            }
        }
    } else {
        out = walk_filesystem(root, opts_, registry);
    }

    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace vectra::cli
