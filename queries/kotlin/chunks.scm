; Chunk-level constructs in Kotlin.

; Type-shaped declarations: classes (including data / sealed / enum
; flavours), interfaces (also class_declaration in this grammar) and
; standalone object declarations.
(class_declaration) @chunk
(object_declaration) @chunk

; Function declarations, top-level or member.
(function_declaration) @chunk

; Top-level property declarations carry meaningful initializers and
; are searched as standalone units (e.g. constants, configuration).
(property_declaration) @chunk
