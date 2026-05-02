; Import / include extraction for Zig.
;
; `const std = @import("std");` reduces to a builtin_function call
; whose builtin_identifier is `@import` and whose first argument is
; a string literal. We anchor the match on the @import identifier
; and capture the string as the imported module path.
((builtin_function
  (builtin_identifier) @_id
  (arguments (string) @import.module))
 (#eq? @_id "@import"))
