; Import / include extraction for HCL / Terraform.
;
; Terraform pulls modules in via `module "..." { source = "..." }`.
; The `source` attribute lives inside the block body — recognising
; it requires matching a block whose identifier is "module" and
; whose body contains an attribute with a specific name. That kind
; of structural pattern is awkward in tree-sitter queries; we leave
; this empty rather than ship a half-working version. The base
; `block` chunks already carry the `source = "..."` line for
; retrieval to surface as content.
