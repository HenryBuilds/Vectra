// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Schema bootstrap for vectra-store. Internal.

#pragma once

#include <sqlite3.h>

namespace vectra::store::detail {

// The schema version this binary was built against. Stored in
// SQLite's user_version pragma; see ensure_schema().
constexpr int kSchemaVersion = 1;

// Apply the schema at version kSchemaVersion to a fresh or
// migrated-up-to-date database. Throws std::runtime_error if the
// database file's version is newer than kSchemaVersion (refusing to
// downgrade) or if any DDL statement fails.
void ensure_schema(sqlite3* db);

}  // namespace vectra::store::detail
