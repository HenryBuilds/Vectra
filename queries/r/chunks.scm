; Chunk-level constructs in R.
;
; A `function_definition` is the value-side of `foo <- function() {...}`.
; Capturing the wrapping `binary_operator` for the assignment shape
; keeps the binding name with the chunk — for raw anonymous
; function literals the function_definition itself stays as a
; chunk.
(binary_operator
  rhs: (function_definition)) @chunk

(function_definition) @chunk
