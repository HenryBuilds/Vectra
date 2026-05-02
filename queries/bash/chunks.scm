; Chunk-level constructs in Bash / shell.
;
; Functions are the only first-class chunk shape in shell scripts.
; Top-level commands are intentionally not chunked individually:
; they are rarely searched as standalone units and grouping them
; into the file-level fallback chunk gives better retrieval.
(function_definition) @chunk
