; Import / include extraction for Clojure.
;
; `(ns foo.bar (:require [some.other :refer [x]]))` is the
; canonical Clojure import shape but it lives several levels deep
; inside a list literal — too structural for a clean tree-sitter
; query. We leave imports empty; the ns-form list_lit chunks that
; chunks.scm captures already carry the require declarations.
