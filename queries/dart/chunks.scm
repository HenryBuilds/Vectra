; Chunk-level constructs in Dart.

; Type-shaped definitions: classes, mixins, enums, extensions, and
; (Dart 3) extension types.
(class_definition) @chunk
(mixin_declaration) @chunk
(enum_declaration) @chunk
(extension_declaration) @chunk
(extension_type_declaration) @chunk

; Top-level functions and methods. The grammar splits the signature
; from the body in some shapes; we capture the signature node
; because it carries the name.
(function_signature) @chunk
