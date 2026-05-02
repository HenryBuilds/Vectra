; Symbol extraction for Lua.
;
; A function_declaration's `name` field can be a plain identifier
; (`function foo()`), a dot-indexed expression (`function M.bar()`),
; or a method-indexed expression (`function obj:method()`). All
; three are useful symbol strings as written.
(function_declaration name: (identifier)              @symbol.function)
(function_declaration name: (dot_index_expression)    @symbol.function)
(function_declaration name: (method_index_expression) @symbol.method)
