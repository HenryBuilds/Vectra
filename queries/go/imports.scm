; Import / include extraction for Go.

; A single import_spec covers both the parenthesised group form
; ("import (...)") and the single-line form ("import \"foo\"") since the
; grammar reduces both to a sequence of import_spec nodes.
(import_spec
  path: (interpreted_string_literal) @import.module)
