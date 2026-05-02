; Symbol extraction for Makefile.
;
; A rule's symbol is its target list. The `targets` child wraps one
; or more pattern words; we capture the wrapper so multi-target
; rules (`a b c: deps`) surface as one symbol string. Variable
; assignments expose their name field directly.
(rule (targets) @symbol.method)
(variable_assignment name: (word) @symbol.field)
