// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/language.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <toml++/toml.hpp>
#include <utility>

#include "grammars.hpp"

namespace vectra::core {

namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(fmt::format("cannot open file: {}", path.string()));
    }
    std::ostringstream buf;
    buf << in.rdbuf();
    return buf.str();
}

[[nodiscard]] std::string require_string(const toml::table& tbl,
                                         std::string_view key,
                                         std::string_view context) {
    const auto* node = tbl.get(key);
    if (node == nullptr || !node->is_string()) {
        throw std::runtime_error(
            fmt::format("languages.toml: missing or non-string field '{}' in {}", key, context));
    }
    return node->value<std::string>().value();
}

[[nodiscard]] std::vector<std::string> require_string_array(const toml::table& tbl,
                                                            std::string_view key,
                                                            std::string_view context) {
    const auto* node = tbl.get(key);
    if (node == nullptr || !node->is_array()) {
        throw std::runtime_error(
            fmt::format("languages.toml: missing or non-array field '{}' in {}", key, context));
    }
    const auto& arr = *node->as_array();
    std::vector<std::string> out;
    out.reserve(arr.size());
    for (const auto& el : arr) {
        if (!el.is_string()) {
            throw std::runtime_error(
                fmt::format("languages.toml: non-string element in '{}' of {}", key, context));
        }
        out.push_back(el.value<std::string>().value());
    }
    return out;
}

}  // namespace

LanguageRegistry LanguageRegistry::from_toml(const std::filesystem::path& manifest_path,
                                             const std::filesystem::path& base_dir) {
    toml::table doc;
    try {
        doc = toml::parse_file(manifest_path.string());
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(fmt::format("languages.toml parse error: {}", e.description()));
    }

    const auto* entries = doc.get_as<toml::array>("language");
    if (entries == nullptr) {
        throw std::runtime_error("languages.toml: top-level 'language' array is missing");
    }

    LanguageRegistry registry;
    registry.languages_.reserve(entries->size());

    for (std::size_t i = 0; i < entries->size(); ++i) {
        const auto* entry = entries->get(i)->as_table();
        if (entry == nullptr) {
            throw std::runtime_error(fmt::format("languages.toml: entry #{} is not a table", i));
        }

        const std::string ctx = fmt::format("[[language]] #{}", i);

        Language lang;
        lang.name = require_string(*entry, "name", ctx);
        lang.extensions = require_string_array(*entry, "extensions", ctx);
        lang.grammar_dir = base_dir / require_string(*entry, "grammar_dir", ctx);
        lang.symbol = require_string(*entry, "symbol", ctx);
        lang.chunks_query_path = base_dir / require_string(*entry, "chunks", ctx);
        lang.symbols_query_path = base_dir / require_string(*entry, "symbols", ctx);
        lang.imports_query_path = base_dir / require_string(*entry, "imports", ctx);

        // Resolve the grammar function pointer at registry build time
        // so we fail fast on a manifest pointing at an unknown symbol.
        lang.ts_language = detail::grammar_by_symbol(lang.symbol);
        if (lang.ts_language == nullptr) {
            throw std::runtime_error(
                fmt::format("languages.toml: unknown grammar symbol '{}' for language '{}'. "
                            "Built-in grammars are registered in src/core/grammars.cpp; add an "
                            "entry there if this is a new built-in language.",
                            lang.symbol,
                            lang.name));
        }

        // Pre-load the query sources so the chunker doesn't hit the
        // filesystem on the hot path. We pay the cost once at startup.
        lang.chunks_query_source = read_file(lang.chunks_query_path);
        lang.symbols_query_source = read_file(lang.symbols_query_path);
        lang.imports_query_source = read_file(lang.imports_query_path);

        registry.languages_.push_back(std::move(lang));
    }

    // Build lookup tables. Lowercased extensions, case-insensitive on
    // input via the for_path() lookup.
    for (std::size_t i = 0; i < registry.languages_.size(); ++i) {
        const auto& l = registry.languages_[i];

        if (auto [_, inserted] = registry.by_name_.emplace(l.name, i); !inserted) {
            throw std::runtime_error(
                fmt::format("languages.toml: duplicate language name '{}'", l.name));
        }

        for (const auto& ext : l.extensions) {
            std::string lowered = ext;
            std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
                return std::tolower(c);
            });
            // First language listing an extension wins; this matches
            // the documented behavior in languages.toml.
            registry.by_extension_.emplace(std::move(lowered), i);
        }
    }

    return registry;
}

const Language* LanguageRegistry::by_name(std::string_view name) const {
    auto it = by_name_.find(std::string{name});
    if (it == by_name_.end())
        return nullptr;
    return &languages_[it->second];
}

const Language* LanguageRegistry::by_extension(std::string_view ext) const {
    std::string lowered{ext};
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](unsigned char c) {
        return std::tolower(c);
    });
    auto it = by_extension_.find(lowered);
    if (it == by_extension_.end())
        return nullptr;
    return &languages_[it->second];
}

const Language* LanguageRegistry::for_path(const std::filesystem::path& path) const {
    auto ext = path.extension().string();
    if (!ext.empty() && ext.front() == '.')
        ext.erase(0, 1);
    if (ext.empty())
        return nullptr;
    return by_extension(ext);
}

}  // namespace vectra::core
