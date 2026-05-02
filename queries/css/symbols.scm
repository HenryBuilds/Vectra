; Symbol extraction for CSS.
;
; The most useful symbol shapes are class names (`.foo`), id names
; (`#foo`), and bare element selectors (`button`). The grammar
; surfaces element selectors as `tag_name` nodes inside the
; selectors list rather than wrapping them in a dedicated
; `type_selector` parent.
(class_selector (class_name) @symbol.type)
(id_selector (id_name) @symbol.type)
(selectors (tag_name) @symbol.type)
