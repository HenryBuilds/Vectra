; Chunk-level constructs in JSON.
;
; Each `pair` is a chunk so retrieval can land on a specific section
; of a config file (`dependencies`, `scripts`, `compilerOptions`, …).
; This intentionally also captures nested pairs — duplication is the
; right call for retrieval over machine-formatted config files where
; the same key path can appear at multiple depths.
(pair) @chunk
