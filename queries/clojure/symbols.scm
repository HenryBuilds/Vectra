; Symbol extraction for Clojure.
;
; The shape `(defn foo [args] body)` puts the function name as the
; second element of the list. We capture the second `sym_lit` (the
; first being `defn`/`def`/`ns`) so the symbol index reflects the
; defined name. This intentionally matches loosely; a top-level
; list like `(println "hi")` will surface "hi" — but the cost is
; low and the gain (catching defn/def/ns names without a custom
; classifier) is high.
(source (list_lit . (sym_lit) (sym_lit) @symbol.function))
