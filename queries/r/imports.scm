; Import / include extraction for R.
;
; `library(ggplot2)`, `require(dplyr)`, `requireNamespace("foo")`
; are normal call expressions. We match calls whose function name
; is one of the standard load primitives and capture the first
; argument as the package name.
(call
  function: (identifier) @_fn
  arguments: (arguments (argument value: [(identifier) (string)] @import.module))
  (#match? @_fn "^(library|require|requireNamespace|loadNamespace|attachNamespace)$"))
