; Symbol extraction for C#.

(class_declaration       name: (identifier) @symbol.type)
(struct_declaration      name: (identifier) @symbol.type)
(interface_declaration   name: (identifier) @symbol.type)
(record_declaration      name: (identifier) @symbol.type)
(enum_declaration        name: (identifier) @symbol.type)
(delegate_declaration    name: (identifier) @symbol.type)

(method_declaration      name: (identifier) @symbol.method)
(constructor_declaration name: (identifier) @symbol.method)
(property_declaration    name: (identifier) @symbol.field)
