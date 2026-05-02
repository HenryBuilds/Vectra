; Chunk-level constructs in Markdown.
;
; A `section` node wraps a heading and the body that follows it up to
; the next heading at the same level. That is the natural retrieval
; unit for documentation: a question about a feature usually maps to
; one section, not the whole file.
(section) @chunk

; Code blocks are also captured as their own chunks. Long code
; samples (tutorials, configs) often outsize their surrounding prose,
; and embedding them separately lets retrieval find them by content
; rather than by the heading they happen to live under.
(fenced_code_block) @chunk
