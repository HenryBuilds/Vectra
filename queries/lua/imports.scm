; Import / include extraction for Lua.
;
; Lua has no import keyword: modules are pulled in via the standard
; `require("foo")` builtin. We match calls whose function name is
; literally `require` and capture the first argument as the module
; path.
(function_call
  name: (identifier) @_fn
  arguments: (arguments (string (string_content) @import.module))
  (#eq? @_fn "require"))
