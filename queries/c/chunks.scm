; Chunk-level constructs in C.
;
; A "chunk" is a unit that gets independently embedded, stored, and
; retrieved. The grain we want is: anything that has its own meaningful
; name AND its own implementation/body block.

; Function definitions carry their full body.
(function_definition) @chunk

; Top-level typedef statements.
(type_definition) @chunk

; Named structs / unions / enums declared at the translation-unit level.
; Anonymous nested forms are intentionally excluded — they are part of
; some other chunk, not a chunk on their own.
(translation_unit
  (struct_specifier
    name: (type_identifier)) @chunk)

(translation_unit
  (union_specifier
    name: (type_identifier)) @chunk)

(translation_unit
  (enum_specifier
    name: (type_identifier)) @chunk)

; Function-like preprocessor macros are first-class chunks because they
; behave like functions to readers.
(preproc_function_def) @chunk
