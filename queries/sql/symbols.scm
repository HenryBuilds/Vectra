; Symbol extraction for SQL.
;
; The created object's name is either a plain `identifier`
; (`CREATE TABLE users …`) or a `dotted_name` (`CREATE TABLE
; public.users …`). Capturing both shapes covers schema-qualified
; and unqualified forms.
(create_table_statement [(identifier) (dotted_name)] @symbol.type)
(create_view_statement [(identifier) (dotted_name)] @symbol.type)
(create_materialized_view_statement [(identifier) (dotted_name)] @symbol.type)

; Triggers and indices use field-typed names rather than naked
; identifier children; matching by field is unambiguous.
(create_trigger_statement name: (identifier) @symbol.function)
(create_index_statement   name: (identifier) @symbol.field)
