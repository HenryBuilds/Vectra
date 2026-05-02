; Chunk-level constructs in Java.

; Top-level type declarations: a class, interface, enum, record, or
; annotation-type definition forms a chunk together with all its
; members. Methods inside are also captured below as their own
; chunks, mirroring how Python and JavaScript behave — both grain
; levels are useful for retrieval.
(class_declaration) @chunk
(interface_declaration) @chunk
(enum_declaration) @chunk
(record_declaration) @chunk
(annotation_type_declaration) @chunk

; Member-level chunks. Methods and constructors get their own chunks
; so retrieval can land directly on a method body without dragging
; the whole containing class along.
(method_declaration) @chunk
(constructor_declaration) @chunk
