; Chunk-level constructs in Go.

(function_declaration) @chunk
(method_declaration) @chunk

; A type_declaration can group several type_specs in one block. We
; capture the whole declaration so the grouping (and the comment on
; the whole block, which Go convention puts above the keyword) lives
; with the chunk.
(type_declaration) @chunk

; Top-level const and var declarations are first-class chunks because
; they often hold meaningful tables (e.g. error definitions, regexp
; constants) that readers search for as a unit.
(const_declaration) @chunk
(var_declaration) @chunk
