; Symbol extraction for Scala.

(class_definition  name: (identifier) @symbol.type)
(object_definition name: (identifier) @symbol.type)
(trait_definition  name: (identifier) @symbol.type)
(enum_definition   name: (identifier) @symbol.type)

(function_definition  name: (identifier) @symbol.function)
(function_declaration name: (identifier) @symbol.function)

(val_definition pattern: (identifier) @symbol.field)
(var_definition pattern: (identifier) @symbol.field)
