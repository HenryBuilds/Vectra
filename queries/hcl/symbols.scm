; Symbol extraction for HCL / Terraform.
;
; A `block` opens with `<identifier> "<label>" "<label>" {...}`. The
; identifier is the kind (`resource`, `module`, `variable`), and the
; string labels follow. The most useful symbol shape is the kind
; itself plus the first label — capturing both lets callers join
; them when they want `resource.aws_instance.web` style names.
(block (identifier) @symbol.type)
(block (string_lit) @symbol.field)

; Free-floating attributes carry their key as the first child name.
(attribute (identifier) @symbol.field)
