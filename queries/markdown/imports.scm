; Import / include extraction for Markdown.
;
; Markdown has no native import concept. Link reference definitions
; (`[label]: url`) are the closest analogue — they declare an
; out-of-line URL the document can refer to — but they are not
; "imports" in any meaningful retrieval sense. We intentionally
; capture nothing here; the file exists so the language manifest
; loader has a query to read.
