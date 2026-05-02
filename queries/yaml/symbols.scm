; Symbol extraction for YAML.
;
; A pair's `key` field is a block_node or flow_node — the wrapping
; node, not the leaf string. Capturing the wrapper means the symbol
; text includes any anchors / tags attached to the key, which is
; what the original source held.
(block_mapping_pair key: (flow_node) @symbol.field)
(block_mapping_pair key: (block_node) @symbol.field)
