; Symbol extraction for Elixir.
;
; The defmodule / def / defp pattern carries its name as the first
; argument of the call. We anchor the match on the call target
; identifier and capture the first argument as the symbol.
((call target: (identifier) @_t
       (arguments [(alias) (identifier) (call)] @symbol.type))
 (#match? @_t "^(defmodule|defprotocol|defimpl|defstruct|defexception)$"))

((call target: (identifier) @_t
       (arguments [(call) (identifier)] @symbol.function))
 (#match? @_t "^(def|defp|defmacro|defmacrop|defguard|defguardp|defdelegate)$"))
