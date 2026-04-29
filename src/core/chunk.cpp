// Copyright 2026 Vectra Contributors. Apache-2.0.

#include "vectra/core/chunk.hpp"

namespace vectra::core {

std::string_view chunk_kind_name(ChunkKind kind) noexcept {
    switch (kind) {
        case ChunkKind::Function:
            return "function";
        case ChunkKind::Method:
            return "method";
        case ChunkKind::Class:
            return "class";
        case ChunkKind::Enum:
            return "enum";
        case ChunkKind::Namespace:
            return "namespace";
        case ChunkKind::Macro:
            return "macro";
        case ChunkKind::TypeAlias:
            return "type_alias";
        case ChunkKind::Constant:
            return "constant";
        case ChunkKind::Other:
            return "other";
        case ChunkKind::Unknown:
            return "unknown";
    }
    return "unknown";
}

}  // namespace vectra::core
