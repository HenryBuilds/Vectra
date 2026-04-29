; Chunk-level constructs in Rust.

(function_item) @chunk

; impl blocks are chunked as one unit even though they contain methods,
; because Rust impls often carry shared trait bounds and type aliases
; that only make sense as a block. The retrieval-time reranker can
; still favor a more specific function inside the impl when relevant.
(impl_item) @chunk

(struct_item) @chunk
(enum_item) @chunk
(union_item) @chunk
(trait_item) @chunk

; Modules are chunks even when they are simple, because a module-level
; doc comment usually frames its contents.
(mod_item) @chunk

; Macros: declarative (macro_definition) and procedural macros declared
; via attribute on a function are both relevant; the function form is
; already covered by function_item, so we only add the declarative form.
(macro_definition) @chunk

; Type-level aliases and top-level constants.
(type_item) @chunk
(const_item) @chunk
(static_item) @chunk
