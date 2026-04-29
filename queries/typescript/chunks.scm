; Chunk-level constructs in TypeScript and TSX.
;
; Inherits the JavaScript shape and adds the TypeScript type-system
; constructs that introduce a named, retrievable unit.

(function_declaration) @chunk
(generator_function_declaration) @chunk
(class_declaration) @chunk
(method_definition) @chunk

(variable_declarator
  value: (arrow_function)) @chunk

(variable_declarator
  value: (function_expression)) @chunk

; TypeScript-specific.
(interface_declaration) @chunk
(type_alias_declaration) @chunk
(enum_declaration) @chunk

; "namespace foo { ... }" blocks are surfaced as the internal_module
; node by the tree-sitter-typescript grammar.
(internal_module) @chunk
