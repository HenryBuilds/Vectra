; Import / include extraction for HTML.
;
; Browsers pull external resources via `<link href="..."` and
; `<script src="..."` attributes. Recognising those requires
; matching elements with specific tag names AND specific attribute
; names — feasible but awkward in tree-sitter queries because the
; structure is element → start_tag → attribute → attribute_value.
; We leave imports empty here; downstream tooling can post-process
; the element chunks if needed.
