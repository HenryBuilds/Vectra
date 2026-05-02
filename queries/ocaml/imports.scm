; Import / include extraction for OCaml.
;
; `open Foo.Bar` parses as `open_module`. We capture the whole node
; so retrieval surfaces the full open declaration including any
; module-path qualifier.
(open_module) @import.module
