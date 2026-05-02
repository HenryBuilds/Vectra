; Chunk-level constructs in Elixir.
;
; Elixir's def/defp/defmodule/defprotocol/defmacro/defimpl are all
; macros — to the parser they look like normal `(call)` expressions
; whose target is one of those identifiers. We capture the wrapping
; call when its target matches a known definition macro, so chunks
; map to functions and module-level structures the way a reader
; would expect.
((call target: (identifier) @_t) @chunk
 (#match? @_t "^(def|defp|defmacro|defmacrop|defmodule|defprotocol|defimpl|defstruct|defexception|defguard|defguardp|defdelegate|deftest)$"))
