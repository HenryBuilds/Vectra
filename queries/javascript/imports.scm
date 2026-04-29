; Import / include extraction for JavaScript.

; ES module imports.
(import_statement
  source: (string) @import.module)

; CommonJS: const x = require("foo")
(call_expression
  function: (identifier) @_require
  (#eq? @_require "require")
  arguments: (arguments
    (string) @import.module))
