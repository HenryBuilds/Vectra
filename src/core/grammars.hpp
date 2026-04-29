// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Resolution layer between the manifest's `symbol` field and the
// linked-in grammar libraries. languages.toml records, for each
// language, the C entry function name (e.g. "tree_sitter_python");
// at runtime we map that string back to the actual function pointer.
//
// Adding a new built-in language requires one new line in this map
// (in grammars.cpp) plus the CMakeLists.txt grammar list. External
// dynamically-loaded grammars will use a separate dlopen path that
// bypasses this table.
//
// Internal to vectra-core.

#pragma once

#include <string_view>

extern "C" {
typedef struct TSLanguage TSLanguage;
}

namespace vectra::core::detail {

// Returns the grammar matching `symbol`, or nullptr if no built-in
// grammar exports that name.
[[nodiscard]] const TSLanguage* grammar_by_symbol(std::string_view symbol) noexcept;

}  // namespace vectra::core::detail
