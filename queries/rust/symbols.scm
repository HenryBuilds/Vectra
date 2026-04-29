; Symbol extraction for Rust.

(function_item    name: (identifier)      @symbol.function)

(struct_item      name: (type_identifier) @symbol.type)
(enum_item        name: (type_identifier) @symbol.type)
(union_item       name: (type_identifier) @symbol.type)
(trait_item       name: (type_identifier) @symbol.type)
(type_item        name: (type_identifier) @symbol.type)

(mod_item         name: (identifier)      @symbol.namespace)

(macro_definition name: (identifier)      @symbol.macro)

(const_item       name: (identifier)      @symbol.constant)
(static_item      name: (identifier)      @symbol.constant)
