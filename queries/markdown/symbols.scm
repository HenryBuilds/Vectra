; Symbol extraction for Markdown.
;
; The "symbols" of a document are its headings. We tag both ATX
; (`# Heading`) and setext (`Heading\n=======`) forms so the symbol
; index reflects the document outline regardless of which style
; the author used.
(atx_heading    heading_content: (inline) @symbol.heading)
(setext_heading heading_content: (paragraph) @symbol.heading)
