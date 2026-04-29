// Copyright 2026 Vectra Contributors. Apache-2.0.
//
// Data types for indexed chunks. A Chunk is the unit of independent
// embedding and retrieval — see architecture.md for what counts as a
// chunk per language. The grammar and chunks.scm query in
// queries/<lang>/ together decide chunk grain.

#pragma once

#include <cstdint>
#include <string>

#include "vectra/core/hash.hpp"

namespace vectra::core {

// Byte and row range within a source file. Rows are 0-indexed, ends
// are exclusive. Tree-sitter reports both byte offsets and row/column
// pairs for free; we keep both because the byte range is what we
// slice on, and the row range is what we report to humans.
struct Range {
    uint32_t start_byte = 0;
    uint32_t end_byte   = 0;
    uint32_t start_row  = 0;
    uint32_t end_row    = 0;
};

// Coarse semantic kind of a chunk. Mapped from the @symbol.<kind>
// captures in queries/<lang>/symbols.scm and from query capture
// names in chunks.scm. Kinds are intentionally generic — language-
// specific concepts (Rust trait, C++ namespace, Python decorator)
// are all squashed into the closest of these buckets so the rest
// of the pipeline (retrieval, ranking) can treat them uniformly.
enum class ChunkKind : uint8_t {
    Unknown   = 0,
    Function  = 1,
    Method    = 2,
    Class     = 3,   // class, struct, union, interface
    Enum      = 4,
    Namespace = 5,   // namespace, module, mod
    Macro     = 6,
    TypeAlias = 7,
    Constant  = 8,
    Other     = 255,
};

[[nodiscard]] std::string_view chunk_kind_name(ChunkKind kind) noexcept;

// One indexed chunk. Owns its `text` rather than referring back into
// the source buffer because the source buffer typically goes out of
// scope before the chunk is embedded or persisted.
struct Chunk {
    std::string  language;       // canonical language name from languages.toml
    ChunkKind    kind = ChunkKind::Unknown;
    std::string  symbol;         // extracted name if available; empty otherwise
    Range        range;
    std::string  text;
    Blake3Hash   content_hash;
};

}  // namespace vectra::core
