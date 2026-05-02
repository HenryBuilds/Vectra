; Chunk-level constructs in PHP.

; Type-shaped declarations: classes (with abstract / final / readonly
; flavours), interfaces, traits, and enums.
(class_declaration) @chunk
(interface_declaration) @chunk
(trait_declaration) @chunk
(enum_declaration) @chunk

; Free functions and class methods. Both are kept so retrieval can
; land on a single function body without dragging the whole class.
(function_definition) @chunk
(method_declaration) @chunk

; Namespace declarations frame a file's top-level grouping. Both the
; block form (`namespace Foo { ... }`) and the file-scoped form
; (`namespace Foo;` followed by the rest of the file) are captured.
(namespace_definition) @chunk
