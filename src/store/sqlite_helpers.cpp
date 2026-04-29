// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "sqlite_helpers.hpp"

#include <fmt/format.h>

#include <stdexcept>

namespace vectra::store::detail {

void check(sqlite3* db, int rc, std::string_view context) {
    if (rc == SQLITE_OK || rc == SQLITE_DONE || rc == SQLITE_ROW)
        return;

    const char* msg = (db != nullptr) ? sqlite3_errmsg(db) : sqlite3_errstr(rc);
    throw std::runtime_error(
        fmt::format("sqlite error in {}: {} (code {})", context, msg ? msg : "unknown", rc));
}

StmtHandle prepare(sqlite3* db, std::string_view sql) {
    sqlite3_stmt* stmt = nullptr;
    const int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        const char* msg = sqlite3_errmsg(db);
        throw std::runtime_error(fmt::format(
            "sqlite prepare failed: {} (code {})\nSQL: {}", msg ? msg : "unknown", rc, sql));
    }
    return StmtHandle{stmt};
}

void exec(sqlite3* db, std::string_view sql) {
    char* err = nullptr;
    // sqlite3_exec needs a null-terminated string; copy to ensure that.
    const std::string copy{sql};
    const int rc = sqlite3_exec(db, copy.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : "unknown";
        sqlite3_free(err);
        throw std::runtime_error(
            fmt::format("sqlite exec failed: {} (code {})\nSQL: {}", msg, rc, sql));
    }
}

}  // namespace vectra::store::detail
