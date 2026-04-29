// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "grammars.hpp"

extern "C" {
const TSLanguage* tree_sitter_c();
const TSLanguage* tree_sitter_cpp();
const TSLanguage* tree_sitter_python();
const TSLanguage* tree_sitter_javascript();
const TSLanguage* tree_sitter_typescript();
const TSLanguage* tree_sitter_tsx();
const TSLanguage* tree_sitter_rust();
const TSLanguage* tree_sitter_go();
}  // extern "C"

namespace vectra::core::detail {

const TSLanguage* grammar_by_symbol(std::string_view symbol) noexcept {
    // Linear scan over a tiny table is faster than any hashing here:
    // we resolve grammars exactly once per language at registry build
    // time, and the table is short. Keep this in sync with the
    // VECTRA_GRAMMARS list in CMakeLists.txt.
    if (symbol == "tree_sitter_c")
        return tree_sitter_c();
    if (symbol == "tree_sitter_cpp")
        return tree_sitter_cpp();
    if (symbol == "tree_sitter_python")
        return tree_sitter_python();
    if (symbol == "tree_sitter_javascript")
        return tree_sitter_javascript();
    if (symbol == "tree_sitter_typescript")
        return tree_sitter_typescript();
    if (symbol == "tree_sitter_tsx")
        return tree_sitter_tsx();
    if (symbol == "tree_sitter_rust")
        return tree_sitter_rust();
    if (symbol == "tree_sitter_go")
        return tree_sitter_go();
    return nullptr;
}

}  // namespace vectra::core::detail
