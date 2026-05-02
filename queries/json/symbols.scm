; Symbol extraction for JSON.
;
; The "symbols" of a JSON file are the keys. Capturing the pair's
; `key` field gives us the string node verbatim, including the
; surrounding quotes — callers strip them when needed.
(pair key: (string) @symbol.field)
