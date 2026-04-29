; Symbol extraction for C.
;
; Each captured node represents an identifier that should be searchable
; via the symbol index (BM25 / trigram). The capture name encodes the
; symbol kind: @symbol.<kind>, where <kind> is one of:
;   function, type, macro, constant, namespace.

; Function names (plain and pointer-returning forms).
(function_definition
  declarator: (function_declarator
    declarator: (identifier) @symbol.function))

(function_definition
  declarator: (pointer_declarator
    declarator: (function_declarator
      declarator: (identifier) @symbol.function)))

; Tag names of structs / unions / enums.
(struct_specifier name: (type_identifier) @symbol.type)
(union_specifier  name: (type_identifier) @symbol.type)
(enum_specifier   name: (type_identifier) @symbol.type)

; typedef names.
(type_definition
  declarator: (type_identifier) @symbol.type)

; Function-like and object-like macros.
(preproc_function_def name: (identifier) @symbol.macro)
(preproc_def          name: (identifier) @symbol.macro)
