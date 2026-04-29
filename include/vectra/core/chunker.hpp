// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// AST-driven chunk extractor. Given a source string and a language,
// runs the language's chunks.scm tree-sitter query against the parse
// tree and emits one Chunk per match.
//
// Chunker is the seam between data (queries, grammars) and the rest
// of the pipeline. It is the only component that knows about
// tree-sitter; everything downstream consumes Chunk values.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "vectra/core/chunk.hpp"
#include "vectra/core/language.hpp"

namespace vectra::core {

class ParserPool;

class Chunker {
public:
    Chunker(const LanguageRegistry& registry, ParserPool& pool);

    // Parse `source` against `lang`'s grammar and return the chunks
    // that the chunks.scm query matches. Throws std::runtime_error
    // if the language's query source fails to compile (which is a
    // bug in the query file, not user input — should be caught by
    // tests before shipping).
    [[nodiscard]] std::vector<Chunk> chunk(std::string_view source,
                                           const Language& lang) const;

    // Convenience overload that resolves the language from a file
    // path's extension. Returns an empty vector for paths whose
    // extension is not registered.
    [[nodiscard]] std::vector<Chunk> chunk_path(std::string_view source,
                                                std::string_view extension) const;

private:
    const LanguageRegistry& registry_;
    ParserPool&             pool_;
};

}  // namespace vectra::core
