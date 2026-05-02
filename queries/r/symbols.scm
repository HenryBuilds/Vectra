; Symbol extraction for R.
;
; Named-function pattern: `name <- function(...) {...}` parses as a
; binary_operator with lhs identifier and rhs function_definition.
; We capture the lhs identifier as the symbol when the rhs shape
; matches.
(binary_operator
  lhs: (identifier) @symbol.function
  rhs: (function_definition))
