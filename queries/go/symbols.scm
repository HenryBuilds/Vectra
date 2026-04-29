; Symbol extraction for Go.

(function_declaration name: (identifier)       @symbol.function)
(method_declaration   name: (field_identifier) @symbol.method)

; type_declaration wraps one or more type_specs. We capture each spec
; individually so a multi-type block produces multiple symbols.
(type_spec name: (type_identifier) @symbol.type)
