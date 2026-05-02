; Import / include extraction for PHP.
;
; `use Foo\Bar;` and `use Foo\{Bar, Baz};` both reduce to a
; namespace_use_declaration whose direct children include one or more
; namespace_use_clause nodes. The grouped form additionally carries a
; namespace_name prefix sibling. We capture both shapes so retrieval
; sees the imported namespace path either way.
(namespace_use_declaration (namespace_use_clause (qualified_name) @import.module))
(namespace_use_declaration (namespace_use_clause (name) @import.module))
(namespace_use_declaration (namespace_name) @import.module)
