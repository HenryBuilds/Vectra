; Chunk-level constructs in C#.

; Type-level declarations form chunks together with their members.
(class_declaration) @chunk
(struct_declaration) @chunk
(interface_declaration) @chunk
(record_declaration) @chunk
(enum_declaration) @chunk
(delegate_declaration) @chunk

; Member-level chunks. Methods, constructors, properties and indexers
; carry meaningful bodies that retrieval should be able to land on
; directly without pulling the whole containing type.
(method_declaration) @chunk
(constructor_declaration) @chunk
(destructor_declaration) @chunk
(property_declaration) @chunk
(indexer_declaration) @chunk
(operator_declaration) @chunk

; Namespace declarations frame a file's top-level grouping. Both the
; classic block form and C# 10's file_scoped form are useful as chunks
; when retrieval cares about which namespace something lives in.
(namespace_declaration) @chunk
(file_scoped_namespace_declaration) @chunk
