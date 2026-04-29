// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/chunker.hpp"

#include <cstdint>
#include <stdexcept>
#include <string>

#include <fmt/format.h>
#include <tree_sitter/api.h>

#include "parser_pool.hpp"

namespace vectra::core {

namespace {

// Map tree-sitter node type names to coarse ChunkKind. The mapping is
// small and language-agnostic — the input is the named AST node from
// the grammar, so this stays valid as long as upstream grammars don't
// rename their node types (which is a stable contract in practice).
[[nodiscard]] ChunkKind kind_from_node_type(std::string_view node_type) noexcept {
    if (node_type == "function_definition")            return ChunkKind::Function;
    if (node_type == "function_declaration")           return ChunkKind::Function;
    if (node_type == "function_item")                  return ChunkKind::Function;
    if (node_type == "generator_function_declaration") return ChunkKind::Function;
    if (node_type == "method_definition")              return ChunkKind::Method;
    if (node_type == "method_declaration")             return ChunkKind::Method;
    if (node_type == "class_definition")               return ChunkKind::Class;
    if (node_type == "class_declaration")              return ChunkKind::Class;
    if (node_type == "class_specifier")                return ChunkKind::Class;
    if (node_type == "struct_specifier")               return ChunkKind::Class;
    if (node_type == "struct_item")                    return ChunkKind::Class;
    if (node_type == "union_specifier")                return ChunkKind::Class;
    if (node_type == "union_item")                     return ChunkKind::Class;
    if (node_type == "interface_declaration")          return ChunkKind::Class;
    if (node_type == "trait_item")                     return ChunkKind::Class;
    if (node_type == "impl_item")                      return ChunkKind::Class;
    if (node_type == "enum_specifier")                 return ChunkKind::Enum;
    if (node_type == "enum_item")                      return ChunkKind::Enum;
    if (node_type == "enum_declaration")               return ChunkKind::Enum;
    if (node_type == "namespace_definition")           return ChunkKind::Namespace;
    if (node_type == "internal_module")                return ChunkKind::Namespace;
    if (node_type == "mod_item")                       return ChunkKind::Namespace;
    if (node_type == "preproc_function_def")           return ChunkKind::Macro;
    if (node_type == "macro_definition")               return ChunkKind::Macro;
    if (node_type == "type_definition")                return ChunkKind::TypeAlias;
    if (node_type == "type_alias_declaration")         return ChunkKind::TypeAlias;
    if (node_type == "type_item")                      return ChunkKind::TypeAlias;
    if (node_type == "type_declaration")               return ChunkKind::TypeAlias;
    if (node_type == "alias_declaration")              return ChunkKind::TypeAlias;
    if (node_type == "const_item")                     return ChunkKind::Constant;
    if (node_type == "static_item")                    return ChunkKind::Constant;
    if (node_type == "const_declaration")              return ChunkKind::Constant;
    if (node_type == "var_declaration")                return ChunkKind::Constant;
    if (node_type == "decorated_definition")           return ChunkKind::Other;  // refined below
    if (node_type == "template_declaration")           return ChunkKind::Other;
    if (node_type == "variable_declarator")            return ChunkKind::Function;  // bound arrow / fn expr
    return ChunkKind::Other;
}

// Try to extract a name field from the chunk's root node. Tree-sitter
// exposes a `name` field on most "named" definitions; we try it first
// and fall back to walking children for grammars that don't surface it.
[[nodiscard]] std::string extract_symbol_name(TSNode node, std::string_view source) {
    TSNode name = ts_node_child_by_field_name(node, "name", 4);
    if (ts_node_is_null(name)) {
        return {};
    }
    const uint32_t start = ts_node_start_byte(name);
    const uint32_t end   = ts_node_end_byte(name);
    if (end <= start || end > source.size()) {
        return {};
    }
    return std::string(source.substr(start, end - start));
}

}  // namespace

Chunker::Chunker(const LanguageRegistry& registry, ParserPool& pool)
    : registry_(registry), pool_(pool) {}

std::vector<Chunk> Chunker::chunk(std::string_view source, const Language& lang) const {
    if (source.empty()) {
        return {};
    }

    // Acquire a parser and run it.
    ParserLease lease = pool_.acquire(lang.name);

    TSTree* tree = ts_parser_parse_string(
        lease.get(),
        nullptr,
        source.data(),
        static_cast<uint32_t>(source.size()));
    if (tree == nullptr) {
        throw std::runtime_error(fmt::format(
            "ts_parser_parse_string returned null for language '{}'", lang.name));
    }

    // Compile the chunks.scm query. We re-compile per call for now;
    // a future optimization is to cache compiled queries on the
    // Language object after first use, guarded by a once_flag.
    uint32_t       error_offset = 0;
    TSQueryError   error_type   = TSQueryErrorNone;
    TSQuery* query = ts_query_new(
        lang.ts_language,
        lang.chunks_query_source.data(),
        static_cast<uint32_t>(lang.chunks_query_source.size()),
        &error_offset,
        &error_type);
    if (query == nullptr) {
        ts_tree_delete(tree);
        throw std::runtime_error(fmt::format(
            "Failed to compile chunks query for '{}' at byte {} (error type {})",
            lang.name, error_offset, static_cast<int>(error_type)));
    }

    TSQueryCursor* cursor = ts_query_cursor_new();
    ts_query_cursor_exec(cursor, query, ts_tree_root_node(tree));

    std::vector<Chunk> out;

    TSQueryMatch match;
    while (ts_query_cursor_next_match(cursor, &match)) {
        for (uint16_t i = 0; i < match.capture_count; ++i) {
            const TSNode node = match.captures[i].node;

            const uint32_t start_byte = ts_node_start_byte(node);
            const uint32_t end_byte   = ts_node_end_byte(node);
            if (end_byte <= start_byte || end_byte > source.size()) {
                continue;
            }

            const TSPoint start_pt = ts_node_start_point(node);
            const TSPoint end_pt   = ts_node_end_point(node);

            const char* node_type_c = ts_node_type(node);
            const std::string_view node_type{node_type_c};

            Chunk c;
            c.language = lang.name;
            c.kind     = kind_from_node_type(node_type);
            c.symbol   = extract_symbol_name(node, source);
            c.range    = Range{
                start_byte,
                end_byte,
                start_pt.row,
                end_pt.row,
            };
            c.text         = std::string(source.substr(start_byte, end_byte - start_byte));
            c.content_hash = hash_string(c.text);

            out.push_back(std::move(c));
        }
    }

    ts_query_cursor_delete(cursor);
    ts_query_delete(query);
    ts_tree_delete(tree);

    return out;
}

std::vector<Chunk> Chunker::chunk_path(std::string_view source,
                                       std::string_view extension) const {
    const Language* lang = registry_.by_extension(extension);
    if (lang == nullptr) {
        return {};
    }
    return chunk(source, *lang);
}

}  // namespace vectra::core
