; Chunk-level constructs in Makefile.
;
; A `rule` is the natural chunk: `target: deps\n\trecipe…`. Variable
; assignments are also captured because Makefiles often carry
; non-trivial command construction in them (`CFLAGS = -Wall -O2 …`)
; that retrieval should be able to land on directly.
(rule) @chunk
(variable_assignment) @chunk
(define_directive) @chunk
(include_directive) @chunk
