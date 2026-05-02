; Chunk-level constructs in Ruby.

; Classes and modules carry their entire body as a chunk. Methods and
; singleton methods (def self.foo) are also captured individually so
; retrieval can land on a single def without pulling the whole class.
(class) @chunk
(module) @chunk
(method) @chunk
(singleton_method) @chunk
