; Chunk-level constructs in Lua.

; Named function declarations (`function foo()`, `function M.bar()`,
; `function obj:method()`) all reduce to function_declaration. The
; anonymous function form (`local f = function() ... end`) shows up
; as variable_declaration whose value is a function_definition; we
; capture the wrapping variable_declaration so the binding name
; travels with the chunk.
(function_declaration) @chunk
(variable_declaration) @chunk
