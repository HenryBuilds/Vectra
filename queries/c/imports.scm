; Import / include extraction for C.
;
; Capture names distinguish where the include points: @import.local for
; "double-quoted" includes (project-relative), @import.system for
; <angle-bracketed> system headers.

(preproc_include
  path: (string_literal) @import.local)

(preproc_include
  path: (system_lib_string) @import.system)
