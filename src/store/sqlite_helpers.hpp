// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Minimal RAII wrappers around the SQLite C API plus a few helpers
// that translate SQLite return codes into exceptions with useful
// messages. Internal to vectra-store.

#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace vectra::store::detail {

// RAII handle for a SQLite connection.
struct SqliteCloser {
    void operator()(sqlite3* db) const noexcept {
        if (db != nullptr)
            sqlite3_close_v2(db);
    }
};
using SqliteHandle = std::unique_ptr<sqlite3, SqliteCloser>;

// RAII handle for a prepared statement. ResetGuard below resets state
// without destroying the prepared statement (cheaper for repeated use).
struct StmtFinalizer {
    void operator()(sqlite3_stmt* s) const noexcept {
        if (s != nullptr)
            sqlite3_finalize(s);
    }
};
using StmtHandle = std::unique_ptr<sqlite3_stmt, StmtFinalizer>;

// Reset a statement when the guard goes out of scope so the same
// prepared statement can be re-bound and re-executed.
class ResetGuard {
public:
    explicit ResetGuard(sqlite3_stmt* stmt) noexcept : stmt_(stmt) {}
    ~ResetGuard() {
        if (stmt_ != nullptr) {
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
    }
    ResetGuard(const ResetGuard&) = delete;
    ResetGuard& operator=(const ResetGuard&) = delete;

private:
    sqlite3_stmt* stmt_;
};

// Throw std::runtime_error if `rc` is not SQLITE_OK / SQLITE_DONE /
// SQLITE_ROW. Includes the SQLite error message for diagnostics.
void check(sqlite3* db, int rc, std::string_view context);

// Prepare a statement; throws on error.
[[nodiscard]] StmtHandle prepare(sqlite3* db, std::string_view sql);

// Execute one or more SQL statements separated by semicolons. Used
// for schema bootstrap, where we have a single multi-statement blob.
void exec(sqlite3* db, std::string_view sql);

// Run a function inside an IMMEDIATE transaction. Commits on normal
// return, rolls back if the function throws.
template <typename F>
void in_transaction(sqlite3* db, F&& fn) {
    exec(db, "BEGIN IMMEDIATE");
    try {
        fn();
    } catch (...) {
        // Best-effort rollback; ignore errors here so we surface the
        // original exception, which is the more useful one.
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
    exec(db, "COMMIT");
}

// Convenience binders. Position is 1-indexed, matching the SQLite C API.
inline void bind_text(sqlite3_stmt* s, int pos, std::string_view v) {
    sqlite3_bind_text(s, pos, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
}
inline void bind_int64(sqlite3_stmt* s, int pos, int64_t v) {
    sqlite3_bind_int64(s, pos, v);
}
inline void bind_int(sqlite3_stmt* s, int pos, int v) {
    sqlite3_bind_int(s, pos, v);
}
inline void bind_blob(sqlite3_stmt* s, int pos, std::span<const std::byte> bytes) {
    sqlite3_bind_blob64(
        s, pos, bytes.data(), static_cast<sqlite3_uint64>(bytes.size()), SQLITE_TRANSIENT);
}
inline void bind_null(sqlite3_stmt* s, int pos) {
    sqlite3_bind_null(s, pos);
}

// Column readers. Position is 0-indexed, matching the SQLite C API.
inline std::string column_text(sqlite3_stmt* s, int pos) {
    const auto* p = sqlite3_column_text(s, pos);
    if (p == nullptr)
        return {};
    const int n = sqlite3_column_bytes(s, pos);
    return {reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)};
}
inline int64_t column_int64(sqlite3_stmt* s, int pos) {
    return sqlite3_column_int64(s, pos);
}
inline int column_int(sqlite3_stmt* s, int pos) {
    return sqlite3_column_int(s, pos);
}
// Returns a view valid until the next call on this statement.
inline std::span<const std::byte> column_blob(sqlite3_stmt* s, int pos) {
    const void* p = sqlite3_column_blob(s, pos);
    const int n = sqlite3_column_bytes(s, pos);
    return {static_cast<const std::byte*>(p), static_cast<std::size_t>(n)};
}

}  // namespace vectra::store::detail
