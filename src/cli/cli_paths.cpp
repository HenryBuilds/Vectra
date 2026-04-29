// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "cli_paths.hpp"

#include <system_error>
#include <vector>

#ifdef _WIN32
#include <io.h>
#include <stdio.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>

#include <climits>
#include <cstdio>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace vectra::cli {

namespace fs = std::filesystem;

namespace {

[[nodiscard]] bool has_marker(const fs::path& dir) noexcept {
    std::error_code ec;
    return fs::exists(dir / ".vectra", ec) || fs::exists(dir / ".git", ec);
}

}  // namespace

std::optional<fs::path> find_project_root(const fs::path& start) {
    std::error_code ec;
    fs::path cur = fs::weakly_canonical(start, ec);
    if (ec) {
        cur = start;
    }
    while (true) {
        if (has_marker(cur)) {
            return cur;
        }
        const auto parent = cur.parent_path();
        if (parent.empty() || parent == cur) {
            return std::nullopt;
        }
        cur = parent;
    }
}

fs::path resolve_config_path(const fs::path& repo_root, const fs::path& override_path) {
    if (!override_path.empty()) {
        return override_path;
    }
    return repo_root / ".vectra" / "config.toml";
}

std::optional<fs::path> resolve_adapters_dir(const fs::path& repo_root,
                                             const fs::path& override_path) {
    std::error_code ec;
    if (!override_path.empty()) {
        if (fs::is_directory(override_path, ec)) {
            return override_path;
        }
        // Explicit path that doesn't exist is a user error — surface
        // it by returning nullopt; the caller emits a clear error.
        return std::nullopt;
    }

    std::vector<fs::path> candidates;
    candidates.push_back(repo_root / "adapters");

    const auto exe = current_exe_path();
    if (!exe.empty()) {
        const auto exe_dir = exe.parent_path();
        candidates.push_back(exe_dir / ".." / "share" / "vectra" / "adapters");
        candidates.push_back(exe_dir / "adapters");
    }

    for (auto& c : candidates) {
        if (fs::is_directory(c, ec)) {
            return fs::weakly_canonical(c, ec);
        }
    }
    return std::nullopt;
}

fs::path current_exe_path() {
#ifdef _WIN32
    // Buffer big enough for long paths under \\?\ prefix.
    std::vector<wchar_t> buf(32768);
    const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
    if (n == 0 || n == buf.size()) {
        return {};
    }
    return fs::path{std::wstring{buf.data(), buf.data() + n}};
#elif defined(__APPLE__)
    char buf[PATH_MAX];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) {
        return {};
    }
    std::error_code ec;
    return fs::canonical(buf, ec);
#elif defined(__linux__)
    char buf[PATH_MAX];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf));
    if (n <= 0 || static_cast<std::size_t>(n) >= sizeof(buf)) {
        return {};
    }
    return fs::path{buf, buf + n};
#else
    return {};
#endif
}

bool stdout_is_tty() noexcept {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

}  // namespace vectra::cli
