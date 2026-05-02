; Chunk-level constructs in OCaml.

; Modules and module types frame a file's top-level grouping.
(module_definition) @chunk
(module_type_definition) @chunk

; Type, value, and class bindings. `value_definition` covers the
; `let foo = …` / `let rec foo a = …` shapes; `let_binding` is the
; child carrying the actual binding, captured separately so a multi-
; clause `let foo = … and bar = …` produces one chunk per clause.
(type_definition) @chunk
(value_definition) @chunk
(class_binding) @chunk
(method_definition) @chunk

; External declarations and module-binding submodules.
(external) @chunk
(module_binding) @chunk
