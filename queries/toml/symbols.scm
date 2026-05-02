; Symbol extraction for TOML.
;
; A `table` header carries one of bare_key / dotted_key / quoted_key
; as its name. We capture all three so retrieval can search for
; `[dependencies]` whether it was written `dependencies` or
; `"dependencies"`. Pairs inside the document carry the same key
; types under the `pair` node.
(table [(bare_key) (dotted_key) (quoted_key)] @symbol.type)
(table_array_element [(bare_key) (dotted_key) (quoted_key)] @symbol.type)
(pair  [(bare_key) (dotted_key) (quoted_key)] @symbol.field)
