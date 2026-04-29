; Chunk-level constructs in C++.

; Function and member-function bodies.
(function_definition) @chunk

; Named class-like declarations. Anonymous class/struct/union forms are
; nested inside other chunks and intentionally not captured here.
(class_specifier  name: (type_identifier)) @chunk
(struct_specifier name: (type_identifier)) @chunk
(union_specifier  name: (type_identifier)) @chunk
(enum_specifier   name: (type_identifier)) @chunk

; Namespaces are chunks because they delimit lookup scopes a reader
; will want surfaced as a unit.
(namespace_definition) @chunk

; A template_declaration wraps its templated entity (function, class,
; alias). Capture the wrapper so the template parameters live in the
; same chunk as the body they parameterize.
(template_declaration) @chunk

; Top-level typedefs and using-aliases.
(type_definition) @chunk
(alias_declaration) @chunk

; Function-like macros.
(preproc_function_def) @chunk
