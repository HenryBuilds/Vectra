; Symbol extraction for Zig.
;
; Only function_declaration carries a `name:` field directly.
; Structs / enums / unions / error sets are values bound by an
; enclosing variable_declaration — their names live one level up,
; which we don't traverse here. The chunker's symbol fallback
; already walks the declarator chain for shapes like that.
(function_declaration name: (identifier) @symbol.function)
(test_declaration (string) @symbol.method)
