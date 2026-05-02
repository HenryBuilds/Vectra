; Import / include extraction for CSS.
;
; `@import url("foo.css");` and `@import "foo.css";` both reduce to
; an `import_statement`. Capturing the whole statement gives us the
; URL plus any media-query suffix that travelled with the import.
(import_statement) @import.module
