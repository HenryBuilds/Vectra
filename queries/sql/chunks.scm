; Chunk-level constructs in SQL.
;
; SQL is statement-oriented. CREATE / ALTER / DROP statements form
; the natural retrieval grain — schema migrations and stored
; procedures are searched as units. Top-level DML (SELECT / INSERT
; / UPDATE / DELETE) is also captured because embedded queries
; often live one statement per file.
(create_table_statement) @chunk
(create_function_statement) @chunk
(create_view_statement) @chunk
(create_materialized_view_statement) @chunk
(create_index_statement) @chunk
(create_type_statement) @chunk
(create_trigger_statement) @chunk
(create_schema_statement) @chunk
(create_role_statement) @chunk
(create_extension_statement) @chunk
(create_domain_statement) @chunk
(create_sequence) @chunk

(alter_table) @chunk
(alter_statement) @chunk
(alter_sequence) @chunk

(drop_statement) @chunk

(select_statement) @chunk
(insert_statement) @chunk
(update_statement) @chunk
(delete_statement) @chunk
