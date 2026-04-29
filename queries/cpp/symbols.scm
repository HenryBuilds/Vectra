; Symbol extraction for C++.

; Free functions.
(function_definition
  declarator: (function_declarator
    declarator: (identifier) @symbol.function))

; Out-of-line member definitions: void Foo::bar() { ... }
(function_definition
  declarator: (function_declarator
    declarator: (qualified_identifier) @symbol.method))

; Inline member definitions inside a class body.
(function_definition
  declarator: (function_declarator
    declarator: (field_identifier) @symbol.method))

; Class-like type names.
(class_specifier  name: (type_identifier) @symbol.type)
(struct_specifier name: (type_identifier) @symbol.type)
(union_specifier  name: (type_identifier) @symbol.type)
(enum_specifier   name: (type_identifier) @symbol.type)

; Namespace names.
(namespace_definition name: (namespace_identifier) @symbol.namespace)

; Type aliases, both legacy typedef and modern using.
(type_definition
  declarator: (type_identifier) @symbol.type)

(alias_declaration
  name: (type_identifier) @symbol.type)

; Macros.
(preproc_function_def name: (identifier) @symbol.macro)
(preproc_def          name: (identifier) @symbol.macro)
