; Chunk-level constructs in JavaScript.

(function_declaration) @chunk
(generator_function_declaration) @chunk
(class_declaration) @chunk

; Methods inside a class. Captured separately from the class so a
; precise method-level hit isn't drowned by its enclosing class.
(method_definition) @chunk

; Arrow functions and function expressions assigned to a variable.
; We capture the variable_declarator (not the function alone) so the
; binding name remains attached to the chunk; an unnamed arrow function
; alone is hard to surface in retrieval results.
(variable_declarator
  value: (arrow_function)) @chunk

(variable_declarator
  value: (function_expression)) @chunk
