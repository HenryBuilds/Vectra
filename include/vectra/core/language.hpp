// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Language registry. Loads languages.toml at startup, resolves each
// entry's grammar symbol against the linked-in grammar libraries,
// and serves lookups by name or by file extension.
//
// The whole point of this layer is that the rest of vectra-core
// never branches on language identity. It iterates Languages,
// dispatches to whichever grammar each one points at, and runs the
// associated query files. Adding a language is a manifest edit.

#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// Forward-declare the tree-sitter language struct so headers don't
// need to expose tree-sitter to consumers.
extern "C" {
typedef struct TSLanguage TSLanguage;
}

namespace vectra::core {

// Description of one supported language. Fields starting with `_`
// are filled in by LanguageRegistry after the TOML parse — they hold
// the resolved grammar pointer and the loaded query source text.
struct Language {
    std::string                       name;
    std::vector<std::string>          extensions;
    std::filesystem::path             grammar_dir;
    std::string                       symbol;
    std::filesystem::path             chunks_query_path;
    std::filesystem::path             symbols_query_path;
    std::filesystem::path             imports_query_path;

    // Resolved at registry construction time.
    const TSLanguage*                 ts_language = nullptr;
    std::string                       chunks_query_source;
    std::string                       symbols_query_source;
    std::string                       imports_query_source;
};

class LanguageRegistry {
public:
    // Load and resolve all entries from a TOML manifest. Paths in the
    // manifest are interpreted relative to `base_dir`, which is
    // typically the repo root or a deployed asset directory.
    //
    // Throws std::runtime_error on malformed TOML, missing query
    // files, or unknown grammar symbols. The exception message points
    // at the offending entry so the user can fix their manifest.
    static LanguageRegistry from_toml(const std::filesystem::path& manifest_path,
                                      const std::filesystem::path& base_dir);

    // Lookup helpers. All return nullptr if no match is found.
    [[nodiscard]] const Language* by_name(std::string_view name) const;
    [[nodiscard]] const Language* by_extension(std::string_view ext) const;
    [[nodiscard]] const Language* for_path(const std::filesystem::path& path) const;

    [[nodiscard]] const std::vector<Language>& all() const noexcept { return languages_; }

private:
    LanguageRegistry() = default;

    std::vector<Language>                       languages_;
    std::unordered_map<std::string, std::size_t> by_name_;
    std::unordered_map<std::string, std::size_t> by_extension_;
};

}  // namespace vectra::core
