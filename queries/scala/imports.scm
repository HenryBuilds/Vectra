; Import / include extraction for Scala.
;
; `import scala.collection.mutable.Map` and `import a.{b, c => d}`
; both reduce to an `import_declaration` node. We capture the whole
; declaration so retrieval surfaces the full import expression
; including any renames; downstream tooling can split it.
(import_declaration) @import.module
