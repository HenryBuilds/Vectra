; Chunk-level constructs in HTML.
;
; Each `element` (a tag pair with content) is a chunk. The script
; and style tags get their own dedicated wrappers in this grammar
; because they switch the lexer mode; we capture all three so
; retrieval can land on a `<section>` block, a `<script>` block, or
; a `<style>` block alike.
(element) @chunk
(script_element) @chunk
(style_element) @chunk
