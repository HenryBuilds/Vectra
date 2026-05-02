; Symbol extraction for OCaml.
;
; Most OCaml definitions surface their name as a direct child node
; (module_name, type_constructor, class_name, method_name) without
; a `name:` field label. type_binding is the exception — it does
; carry a `name:` field. let_binding's pattern is field-typed and
; can be a value_name, tuple, or destructured form; we capture the
; whole pattern as `(_)`.
(module_binding (module_name) @symbol.type)
(module_type_definition (module_type_name) @symbol.type)
(type_binding name: (type_constructor) @symbol.type)
(class_binding (class_name) @symbol.type)
(method_definition (method_name) @symbol.method)

(let_binding pattern: (_) @symbol.function)
