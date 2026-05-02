; Import / include extraction for Java.
;
; The grammar represents the path inside `import foo.bar.Baz;` as either
; a single identifier (rare) or a scoped_identifier (the common case),
; with a trailing asterisk node when the import is `import foo.*;`. We
; capture the path node itself so the asterisk does not pollute the
; module string.
(import_declaration [(identifier) (scoped_identifier)] @import.module)
