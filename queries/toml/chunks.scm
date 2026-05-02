; Chunk-level constructs in TOML.
;
; Each `[table]` and `[[array_table]]` becomes a chunk together with
; its body. That matches how people read TOML in practice — by
; section. Top-level `pair` nodes (those before any table header)
; are also captured so a flat manifest like a simple Cargo workspace
; is not lost.
(table) @chunk
(table_array_element) @chunk
(pair) @chunk
