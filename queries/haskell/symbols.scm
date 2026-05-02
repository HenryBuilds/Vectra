; Symbol extraction for Haskell.
;
; A `function`'s `name` field holds either a prefix identifier
; (`foo`) or a backticked / operator form. Capturing the field as
; `(_)` is the simplest correct match across that union.
(function  name: (_) @symbol.function)
(signature name: (_) @symbol.function)
