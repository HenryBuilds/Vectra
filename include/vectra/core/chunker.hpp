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

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vectra/core/chunk.hpp"
#include "vectra/core/language.hpp"

namespace vectra::core {

class ParserPool;  // internal; defined in src/core/parser_pool.hpp

class Chunker {
public:
    // Construct a Chunker that owns its own thread-safe parser pool.
    // The registry must outlive the Chunker.
    explicit Chunker(const LanguageRegistry& registry);

    ~Chunker();
    Chunker(const Chunker&) = delete;
    Chunker& operator=(const Chunker&) = delete;
    // Move-only: move-assignment is omitted because the class holds a
    // reference to LanguageRegistry, which cannot be rebound. Callers
    // that want to swap chunkers should construct fresh ones.
    Chunker(Chunker&&) noexcept;
    Chunker& operator=(Chunker&&) = delete;

    // Parse `source` against `lang`'s grammar and return the chunks
    // that the chunks.scm query matches. Throws std::runtime_error
    // if the language's query source fails to compile (which is a
    // bug in the query file, not user input — should be caught by
    // tests before shipping).
    [[nodiscard]] std::vector<Chunk> chunk(std::string_view source, const Language& lang) const;

    // Convenience overload that resolves the language from a file
    // path's extension. Returns an empty vector for paths whose
    // extension is not registered.
    [[nodiscard]] std::vector<Chunk> chunk_path(std::string_view source,
                                                std::string_view extension) const;

private:
    const LanguageRegistry& registry_;
    std::unique_ptr<ParserPool> pool_;
};

}  // namespace vectra::core
