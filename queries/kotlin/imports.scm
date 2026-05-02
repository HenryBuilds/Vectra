; Import / include extraction for Kotlin.
;
; `import com.example.Foo` and `import com.example.*` both reduce to
; an `import_header` whose first child is the dotted `identifier`
; path. Wildcard imports add a `wildcard_import` sibling we do not
; capture — the path itself is what indexes the dependency.
(import_header (identifier) @import.module)
