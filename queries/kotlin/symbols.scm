; Symbol extraction for Kotlin.
;
; Kotlin's grammar (fwcd) does not expose `name:` field labels on
; declarations: the name is just a direct child node of a known type.
; class_declaration and object_declaration carry exactly one direct
; type_identifier child (the type's name); function_declaration
; carries exactly one direct simple_identifier child (the function
; name). Matching by direct-child type is therefore unambiguous.
(class_declaration    (type_identifier) @symbol.type)
(object_declaration   (type_identifier) @symbol.type)
(function_declaration (simple_identifier) @symbol.function)
