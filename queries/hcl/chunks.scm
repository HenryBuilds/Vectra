; Chunk-level constructs in HCL / Terraform.
;
; Each `block` is a chunk: `resource "aws_..." {...}`,
; `module "vpc" {...}`, `data "..." {...}`, `variable {...}`,
; `provider {...}` and so on all reduce to the same `block` node.
; Top-level `attribute` nodes (free-floating `key = value` lines)
; are captured so a flat .tfvars file is not lost.
(block) @chunk
(attribute) @chunk
