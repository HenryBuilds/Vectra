; Import / include extraction for Ruby.
;
; Ruby has no dedicated import keyword: requires are method calls. We
; recognize the four standard load primitives by name and capture the
; first string argument as the imported module path.
(call
  method: (identifier) @_method
  arguments: (argument_list (string (string_content) @import.module))
  (#match? @_method "^(require|require_relative|load|autoload)$"))
