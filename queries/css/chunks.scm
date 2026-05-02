; Chunk-level constructs in CSS.
;
; `rule_set` covers `selector { declarations }`. `at_rule` covers
; one-line directives like `@charset`, `@import`. `media_statement`
; and other compound at-rules wrap a body and are kept as their own
; chunks so the surrounding `@media (...)` query travels with the
; rules inside.
(rule_set) @chunk
(at_rule) @chunk
(media_statement) @chunk
(import_statement) @chunk
(keyframes_statement) @chunk
(supports_statement) @chunk
