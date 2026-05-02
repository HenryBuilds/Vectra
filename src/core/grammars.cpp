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
const TSLanguage* tree_sitter_java();
const TSLanguage* tree_sitter_ruby();
const TSLanguage* tree_sitter_c_sharp();
const TSLanguage* tree_sitter_bash();
const TSLanguage* tree_sitter_kotlin();
const TSLanguage* tree_sitter_php();
const TSLanguage* tree_sitter_markdown();
const TSLanguage* tree_sitter_json();
const TSLanguage* tree_sitter_yaml();
const TSLanguage* tree_sitter_toml();
const TSLanguage* tree_sitter_dockerfile();
const TSLanguage* tree_sitter_hcl();
const TSLanguage* tree_sitter_make();
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
    if (symbol == "tree_sitter_java")
        return tree_sitter_java();
    if (symbol == "tree_sitter_ruby")
        return tree_sitter_ruby();
    if (symbol == "tree_sitter_c_sharp")
        return tree_sitter_c_sharp();
    if (symbol == "tree_sitter_bash")
        return tree_sitter_bash();
    if (symbol == "tree_sitter_kotlin")
        return tree_sitter_kotlin();
    if (symbol == "tree_sitter_php")
        return tree_sitter_php();
    if (symbol == "tree_sitter_markdown")
        return tree_sitter_markdown();
    if (symbol == "tree_sitter_json")
        return tree_sitter_json();
    if (symbol == "tree_sitter_yaml")
        return tree_sitter_yaml();
    if (symbol == "tree_sitter_toml")
        return tree_sitter_toml();
    if (symbol == "tree_sitter_dockerfile")
        return tree_sitter_dockerfile();
    if (symbol == "tree_sitter_hcl")
        return tree_sitter_hcl();
    if (symbol == "tree_sitter_make")
        return tree_sitter_make();
    return nullptr;
}

}  // namespace vectra::core::detail
