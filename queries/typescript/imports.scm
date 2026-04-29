; Import / include extraction for TypeScript and TSX.

; Standard and type-only ES module imports both surface as
; import_statement with a string source — one query handles both.
(import_statement
  source: (string) @import.module)

; CommonJS interop is still common in TS code.
(call_expression
  function: (identifier) @_require
  (#eq? @_require "require")
  arguments: (arguments
    (string) @import.module))
