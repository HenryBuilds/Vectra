; Symbol extraction for JavaScript.

(function_declaration
  name: (identifier) @symbol.function)

(generator_function_declaration
  name: (identifier) @symbol.function)

(class_declaration
  name: (identifier) @symbol.type)

(method_definition
  name: (property_identifier) @symbol.method)

; Bound arrow functions and function expressions: take the binding name
; as the symbol so "const renderUser = (...) => {...}" surfaces as
; the function "renderUser".
(variable_declarator
  name: (identifier) @symbol.function
  value: [(arrow_function) (function_expression)])
