; Symbol extraction for PHP.
;
; PHP's grammar uses `name` as both the field label and the node
; type of the identifier itself, hence the `name: (name)` shape.
(class_declaration     name: (name) @symbol.type)
(interface_declaration name: (name) @symbol.type)
(trait_declaration     name: (name) @symbol.type)
(enum_declaration      name: (name) @symbol.type)

(function_definition name: (name) @symbol.function)
(method_declaration  name: (name) @symbol.method)
