; Symbol extraction for HTML.
;
; The "symbol" of an element is its tag name. For nested elements
; this captures every level — that's intentional, since we want
; `<form>`, `<input>`, `<button>` all to be findable.
(start_tag (tag_name) @symbol.type)
(self_closing_tag (tag_name) @symbol.type)
