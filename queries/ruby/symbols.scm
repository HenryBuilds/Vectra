; Symbol extraction for Ruby.
;
; The `name:` field on a Ruby method is the hidden `_method_name`
; supertype, which collapses to identifier / constant / setter /
; operator depending on the syntax. Capturing the field as `(_)`
; matches whichever concrete node the parser produced.
;
; Class and module names are similarly either a `constant` (the common
; case: `class Foo`) or a `scope_resolution` (`class Foo::Bar`), so we
; use the same wildcard pattern.
(class            name: (_) @symbol.type)
(module           name: (_) @symbol.type)
(method           name: (_) @symbol.method)
(singleton_method name: (_) @symbol.method)
