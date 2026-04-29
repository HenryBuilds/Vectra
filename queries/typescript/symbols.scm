; Symbol extraction for TypeScript and TSX.

(function_declaration
  name: (identifier) @symbol.function)

(generator_function_declaration
  name: (identifier) @symbol.function)

(class_declaration
  name: (type_identifier) @symbol.type)

(method_definition
  name: (property_identifier) @symbol.method)

(variable_declarator
  name: (identifier) @symbol.function
  value: [(arrow_function) (function_expression)])

; TypeScript-specific.
(interface_declaration  name: (type_identifier) @symbol.type)
(type_alias_declaration name: (type_identifier) @symbol.type)
(enum_declaration       name: (identifier)      @symbol.type)
(internal_module        name: (identifier)      @symbol.namespace)
