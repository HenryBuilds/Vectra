; Chunk-level constructs in YAML.
;
; Each block_mapping_pair becomes a chunk so retrieval can target a
; specific key path (`jobs.test.steps`, `services.db.image`, …). The
; same caveat as JSON applies: nested pairs are also captured, which
; is the right tradeoff for retrieval over machine-formatted configs.
(block_mapping_pair) @chunk
