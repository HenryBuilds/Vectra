// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Patch application with backup and rollback.
//
// The applier takes a Patch produced by parse_unified_diff() and
// changes the working tree in three deterministic phases:
//
//   1. Validate
//      Every hunk's context lines (' ' and '-' prefixes) are
//      compared against the on-disk file at hunk.old_start. If any
//      mismatch is found, we throw before touching the disk — a
//      single bad hunk never produces a half-applied patch.
//
//   2. Backup
//      Every file the patch touches is copied verbatim into
//      `<backup_dir>/<relative path>` before any rewrite. New
//      files get a zero-byte marker so a rollback knows to delete
//      them rather than restore-from-empty.
//
//   3. Apply
//      Files are written with the in-memory result of replaying
//      the hunks. Writes are direct (no temp-file rename) because
//      the backup makes them idempotently recoverable.
//
// On any apply-phase failure (disk full, permission, etc.) we
// raise. The caller can then call rollback() with the returned
// backup directory to restore the working tree.

#pragma once

#include <filesystem>
#include <vector>

#include "vectra/exec/diff.hpp"

namespace vectra::exec {

struct ApplyOptions {
    // All paths in the patch are resolved relative to this directory.
    // Required.
    std::filesystem::path repo_root;

    // Directory under which backup copies are written. Empty means
    // "auto-generate" — the applier creates
    // `<repo_root>/.vectra/backups/<timestamp>/`.
    std::filesystem::path backup_dir;

    // When true, the applier validates and reports what would
    // change but does not write anything. Useful for preview.
    bool dry_run = false;
};

struct ApplyResult {
    std::vector<std::filesystem::path> files_modified;
    std::vector<std::filesystem::path> files_created;
    std::vector<std::filesystem::path> files_deleted;

    // Path to the backup directory, populated even on dry-run (it
    // is created up-front so the caller can predict the path the
    // real run would use).
    std::filesystem::path backup_dir;
};

// Apply the patch to the working tree.
//
// Throws std::runtime_error on:
//   - Missing required option (e.g. empty repo_root).
//   - Context mismatch — a hunk's expected old lines do not match
//     the on-disk file.
//   - Filesystem error (permission denied, disk full, ...).
//
// On success returns the lists of paths that were modified /
// created / deleted plus the backup directory the caller can pass
// to rollback() if a downstream step (compile, test) fails.
[[nodiscard]] ApplyResult apply_patch(const Patch& patch, const ApplyOptions& opts);

// Undo a previous apply_patch() by restoring everything from
// `backup_dir`. Files that are present in the backup are copied
// back over their working-tree counterparts; the zero-byte
// markers for newly-created files are interpreted as "delete".
//
// Throws std::runtime_error on filesystem errors. Best-effort
// recovery is the right policy here — we keep going through the
// rest of the backup even if one entry fails, then report.
void rollback(const std::filesystem::path& backup_dir);

}  // namespace vectra::exec
