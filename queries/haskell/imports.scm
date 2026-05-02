; Import / include extraction for Haskell.
;
; `import qualified Data.Map as M (lookup, insert)` reduces to
; an `import` node. We capture the whole node so retrieval can
; surface the full import line including `qualified`, `as` alias,
; and the parenthesised name list.
(import) @import.module
