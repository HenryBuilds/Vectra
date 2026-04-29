; Chunk-level constructs in Python.

; Plain function and class definitions, anywhere in the tree (top-level
; or nested). Methods inside a class are captured here and will appear
; alongside the class itself — that is intentional, both grain levels
; are useful for retrieval.
(function_definition) @chunk
(class_definition) @chunk

; A decorated_definition wraps a function or class with one or more
; decorators. We capture the wrapper so the decorators travel with the
; chunk body — losing the @decorator line would change semantics.
(decorated_definition) @chunk
