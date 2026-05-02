; Import / include extraction for Elixir.
;
; Elixir pulls modules in via the import / alias / require / use
; macros. They are normal `(call)` expressions whose target
; identifier is one of those names; the first argument is the
; module being referenced.
((call target: (identifier) @_t
       (arguments [(alias) (call) (identifier)] @import.module))
 (#match? @_t "^(import|alias|require|use)$"))
