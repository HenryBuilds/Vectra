; Import / include extraction for Dockerfile.
;
; The base images pulled in by FROM lines are the closest analogue
; to imports — they declare the file's external dependencies. The
; image_spec child of from_instruction holds the `repo:tag@digest`
; form verbatim.
(from_instruction (image_spec) @import.module)
