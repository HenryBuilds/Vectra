; Chunk-level constructs in Scala.

; Type-shaped definitions: classes (incl. case classes), objects,
; traits, enums (Scala 3), and given instances.
(class_definition) @chunk
(object_definition) @chunk
(trait_definition) @chunk
(enum_definition) @chunk
(given_definition) @chunk
(extension_definition) @chunk

; Functions (defs) in concrete and abstract form.
(function_definition) @chunk
(function_declaration) @chunk

; Top-level type / val / var bindings carry meaningful initializers.
(type_definition) @chunk
(val_definition) @chunk
(var_definition) @chunk
