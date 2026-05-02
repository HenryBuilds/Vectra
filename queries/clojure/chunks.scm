; Chunk-level constructs in Clojure.
;
; Clojure is a Lisp: every top-level form is a `list_lit`. The
; grammar does not distinguish defn / def / ns / defmacro at the
; node-type level — they all parse as plain lists. We capture each
; top-level list as a chunk; further classification (which list is
; a function definition vs. a macro vs. a side-effecting form)
; would require predicate matching on the first symbol and is
; deferred to downstream tooling.
(source (list_lit) @chunk)
