// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/exec/apply.hpp"

#include <fmt/format.h>

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vectra::exec {

namespace {

namespace fs = std::filesystem;

// Read a file and split it into LF-terminated lines. Trailing CR
// characters are stripped so a CRLF source round-trips through our
// LF-only internal representation. The return value's size equals
// the number of logical lines; an empty file yields an empty vector.
[[nodiscard]] std::vector<std::string> read_lines(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(fmt::format("cannot open {} for reading", path.string()));
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    const std::string text = buf.str();

    std::vector<std::string> out;
    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const auto end = text.find('\n', cursor);
        if (end == std::string::npos) {
            std::string line = text.substr(cursor);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            out.push_back(std::move(line));
            break;
        }
        std::string line = text.substr(cursor, end - cursor);
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(std::move(line));
        cursor = end + 1;
    }
    return out;
}

// Write `lines` back to `path`, joined by '\n' with a trailing
// newline. If the file did not previously end in a newline, the
// caller's hunk should have included a "\\ No newline" marker —
// not yet honoured in v1; we always emit a final '\n'.
void write_lines(const fs::path& path, const std::vector<std::string>& lines) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(fmt::format("cannot open {} for writing", path.string()));
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        out.write(lines[i].data(), static_cast<std::streamsize>(lines[i].size()));
        out.put('\n');
    }
}

// Validate one hunk against the on-disk file lines it claims to
// modify. Throws on context mismatch with a precise location.
void validate_hunk(const Hunk& hunk,
                   const std::vector<std::string>& file_lines,
                   const std::string& file_path) {
    // Convert to 0-indexed start.
    const std::int32_t start_zero = hunk.old_start > 0 ? hunk.old_start - 1 : 0;

    if (hunk.old_count > 0 &&
        static_cast<std::size_t>(start_zero + hunk.old_count) > file_lines.size()) {
        throw std::runtime_error(
            fmt::format("hunk at {}:{} extends past end of file (file has {} lines)",
                        file_path,
                        hunk.old_start,
                        file_lines.size()));
    }

    std::int32_t cursor = start_zero;
    for (const auto& line : hunk.lines) {
        if (line.empty())
            continue;
        const char prefix = line.front();
        if (prefix == '+' || prefix == '\\') {
            // Added lines and "\\ No newline" markers do not
            // appear in the old file.
            continue;
        }
        const std::string expected = line.substr(1);
        if (static_cast<std::size_t>(cursor) >= file_lines.size()) {
            throw std::runtime_error(
                fmt::format("hunk at {}:{} expects more old lines than the file has",
                            file_path,
                            hunk.old_start));
        }
        if (file_lines[static_cast<std::size_t>(cursor)] != expected) {
            throw std::runtime_error(
                fmt::format("context mismatch at {}:{}\n  expected: '{}'\n  found:    '{}'",
                            file_path,
                            cursor + 1,
                            expected,
                            file_lines[static_cast<std::size_t>(cursor)]));
        }
        ++cursor;
    }
}

// Rebuild the file body by applying every hunk in order. The result
// is the new contents in line form.
[[nodiscard]] std::vector<std::string> apply_hunks(const std::vector<Hunk>& hunks,
                                                   const std::vector<std::string>& original) {
    std::vector<std::string> out;
    std::size_t cursor = 0;  // 0-indexed position in `original`

    for (const auto& hunk : hunks) {
        const std::size_t hunk_start =
            hunk.old_start > 0 ? static_cast<std::size_t>(hunk.old_start - 1) : 0;

        // Copy untouched lines preceding this hunk.
        while (cursor < hunk_start && cursor < original.size()) {
            out.push_back(original[cursor++]);
        }

        // Replay the hunk body: context and additions enter `out`,
        // deletions just advance the source cursor.
        for (const auto& line : hunk.lines) {
            if (line.empty()) {
                out.emplace_back();
                ++cursor;
                continue;
            }
            switch (line.front()) {
                case ' ':
                    out.push_back(line.substr(1));
                    ++cursor;
                    break;
                case '+':
                    out.push_back(line.substr(1));
                    break;
                case '-':
                    ++cursor;
                    break;
                case '\\':
                    // "\\ No newline at end of file" — no-op in v1.
                    break;
                default:
                    // parse_unified_diff already rejects this; keep
                    // a defensive branch.
                    throw std::runtime_error(
                        fmt::format("unexpected hunk line during apply: {}", line));
            }
        }
    }

    // Trailing unchanged lines after the last hunk.
    while (cursor < original.size()) {
        out.push_back(original[cursor++]);
    }
    return out;
}

[[nodiscard]] fs::path resolve(const fs::path& root, const std::string& rel) {
    fs::path joined = root / rel;
    return joined.lexically_normal();
}

[[nodiscard]] fs::path generate_backup_dir(const fs::path& root) {
    const auto now = std::chrono::system_clock::now();
    const auto seconds =
        std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    return root / ".vectra" / "backups" / std::to_string(seconds);
}

// Backup `target` into `backup_dir/<rel>`, preserving the
// repo-relative subdirectory structure. For a file that does not
// yet exist (new-file case) we write a zero-byte marker so that
// rollback knows to delete rather than restore-from-empty.
void backup_file(const fs::path& repo_root,
                 const fs::path& target,
                 const fs::path& backup_dir,
                 bool is_new) {
    const auto rel = target.lexically_relative(repo_root);
    const auto dest = backup_dir / rel;
    fs::create_directories(dest.parent_path());

    if (is_new) {
        std::ofstream marker(dest, std::ios::binary | std::ios::trunc);
        // Write a single byte tag so rollback can distinguish
        // "delete this" from "restore zero-byte file as-is" if
        // anyone ever produces one.
        marker.put('\0');
        return;
    }

    std::error_code ec;
    fs::copy_file(target, dest, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        throw std::runtime_error(
            fmt::format("failed to back up {}: {}", target.string(), ec.message()));
    }
}

}  // namespace

ApplyResult apply_patch(const Patch& patch, const ApplyOptions& opts) {
    if (opts.repo_root.empty()) {
        throw std::runtime_error("apply_patch: repo_root is required");
    }
    if (!fs::is_directory(opts.repo_root)) {
        throw std::runtime_error(
            fmt::format("apply_patch: repo_root is not a directory: {}", opts.repo_root.string()));
    }

    ApplyResult result;
    result.backup_dir =
        opts.backup_dir.empty() ? generate_backup_dir(opts.repo_root) : opts.backup_dir;

    if (!opts.dry_run) {
        fs::create_directories(result.backup_dir);
    }

    // ---- Phase 1: validate every hunk against the working tree --------
    // We read each affected file once and keep its lines in memory so
    // phase 2 can write the new content from the same buffer without
    // re-reading.
    struct Staged {
        const FileDiff* diff;
        fs::path abs;
        std::vector<std::string> original;
        std::vector<std::string> updated;
    };
    std::vector<Staged> staged;
    staged.reserve(patch.files.size());

    for (const auto& file : patch.files) {
        Staged s{&file, {}, {}, {}};

        if (file.is_new_file) {
            s.abs = resolve(opts.repo_root, file.new_path);
            if (fs::exists(s.abs)) {
                throw std::runtime_error(
                    fmt::format("patch creates {} but the file already exists", s.abs.string()));
            }
            // For a new file the hunk's "old" lines are empty by
            // construction; we just collect the '+' lines.
            for (const auto& hunk : file.hunks) {
                for (const auto& line : hunk.lines) {
                    if (line.empty() || line.front() == '+') {
                        s.updated.push_back(line.empty() ? std::string{} : line.substr(1));
                    }
                }
            }
        } else if (file.is_deleted) {
            s.abs = resolve(opts.repo_root, file.old_path);
            if (!fs::exists(s.abs)) {
                throw std::runtime_error(
                    fmt::format("patch deletes {} but the file does not exist", s.abs.string()));
            }
            // No content to compute; the apply step just unlinks.
        } else {
            s.abs = resolve(opts.repo_root, file.old_path.empty() ? file.new_path : file.old_path);
            s.original = read_lines(s.abs);
            for (const auto& hunk : file.hunks) {
                validate_hunk(hunk, s.original, s.abs.string());
            }
            s.updated = apply_hunks(file.hunks, s.original);
        }

        staged.push_back(std::move(s));
    }

    if (opts.dry_run) {
        // Populate the result lists for the user but skip the
        // writes themselves.
        for (const auto& s : staged) {
            if (s.diff->is_new_file)
                result.files_created.push_back(s.abs);
            else if (s.diff->is_deleted)
                result.files_deleted.push_back(s.abs);
            else
                result.files_modified.push_back(s.abs);
        }
        return result;
    }

    // ---- Phase 2: backup every affected file --------------------------
    for (const auto& s : staged) {
        backup_file(opts.repo_root, s.abs, result.backup_dir, s.diff->is_new_file);
    }

    // ---- Phase 3: actually mutate the working tree --------------------
    for (const auto& s : staged) {
        if (s.diff->is_deleted) {
            std::error_code ec;
            fs::remove(s.abs, ec);
            if (ec) {
                throw std::runtime_error(
                    fmt::format("failed to delete {}: {}", s.abs.string(), ec.message()));
            }
            result.files_deleted.push_back(s.abs);
        } else {
            write_lines(s.abs, s.updated);
            if (s.diff->is_new_file) {
                result.files_created.push_back(s.abs);
            } else {
                result.files_modified.push_back(s.abs);
            }
        }
    }

    return result;
}

void rollback(const fs::path& backup_dir) {
    if (!fs::is_directory(backup_dir)) {
        throw std::runtime_error(fmt::format("rollback: not a directory: {}", backup_dir.string()));
    }

    // The backup directory mirrors the repo layout. Every file in
    // it maps to a file in the working tree at the same relative
    // path. The repo root is the parent of `.vectra/backups/...` —
    // we recover it from the convention rather than asking the
    // caller to pass it again.
    fs::path repo_root = backup_dir.parent_path().parent_path().parent_path();
    if (repo_root.empty() || !fs::is_directory(repo_root)) {
        throw std::runtime_error(fmt::format("rollback: cannot infer repo_root from backup path {}",
                                             backup_dir.string()));
    }

    std::vector<std::string> failures;
    for (const auto& entry : fs::recursive_directory_iterator(backup_dir)) {
        if (!entry.is_regular_file())
            continue;
        const auto rel = entry.path().lexically_relative(backup_dir);
        const auto target = repo_root / rel;

        std::error_code ec;
        // A zero-byte single-NUL marker means the file was created
        // by the apply we are now undoing — delete rather than
        // restore.
        if (entry.file_size() == 1) {
            std::ifstream probe(entry.path(), std::ios::binary);
            char c = '\xFF';
            probe.read(&c, 1);
            if (c == '\0') {
                fs::remove(target, ec);
                if (ec) {
                    failures.push_back(fmt::format("delete {}: {}", target.string(), ec.message()));
                }
                continue;
            }
        }

        fs::create_directories(target.parent_path(), ec);
        ec.clear();
        fs::copy_file(entry.path(), target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            failures.push_back(fmt::format("restore {}: {}", target.string(), ec.message()));
        }
    }

    if (!failures.empty()) {
        std::string msg = "rollback completed with errors:";
        for (const auto& f : failures)
            msg += "\n  " + f;
        throw std::runtime_error(msg);
    }
}

}  // namespace vectra::exec
