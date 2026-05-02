; Import / include extraction for Makefile.
;
; `include path/to/foo.mk` and `-include …` (optional include) both
; reduce to an `include_directive` whose `filenames` field wraps
; the list of paths. Capturing the list as a whole lets retrieval
; surface the full include line; downstream tooling can split it.
(include_directive filenames: (list) @import.module)
