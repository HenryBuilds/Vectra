// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "walker.hpp"

#include <algorithm>
#include <system_error>

#include "vectra/core/language.hpp"

namespace vectra::cli {

FileWalker::FileWalker() noexcept : FileWalker(Options{}) {}

FileWalker::FileWalker(Options opts) noexcept : opts_(std::move(opts)) {}

std::vector<std::filesystem::path> FileWalker::walk(const std::filesystem::path& root,
                                                    const core::LanguageRegistry& registry) const {
    namespace fs = std::filesystem;

    std::vector<fs::path> out;
    if (!fs::exists(root)) {
        return out;
    }

    // recursive_directory_iterator with disable_recursion_pending() lets us
    // prune directories without paying for descending into them. The
    // skip_permission_denied flag prevents one unreadable subdirectory
    // from aborting the whole walk.
    fs::directory_options dopts = fs::directory_options::skip_permission_denied;
    if (opts_.follow_symlinks) {
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
            // A single bad entry should not abort the walk. Reset and
            // continue with the next sibling.
            ec.clear();
            continue;
        }

        const fs::directory_entry& entry = *it;
        const fs::path& path = entry.path();
        const std::string name = path.filename().string();

        // Prune ignored directories before descending into them.
        if (entry.is_directory(ec)) {
            if (opts_.ignore_dirs.contains(name)) {
                it.disable_recursion_pending();
            }
            continue;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }

        // Skip files we have no language for. The registry returns
        // nullptr for unregistered extensions; that is the right
        // signal to skip without warning.
        if (registry.for_path(path) == nullptr) {
            continue;
        }

        // Reject oversized files; these are usually generated and
        // would dominate retrieval cost without adding signal.
        const auto size = entry.file_size(ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (size > opts_.max_file_size_bytes) {
            continue;
        }

        out.push_back(path);
    }

    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace vectra::cli
