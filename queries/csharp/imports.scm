; Import / include extraction for C#.
;
; A `using_directive` covers `using System;`, `using System.IO;` and
; `using Foo = System.Collections.Generic.List<int>;`. The namespace
; or aliased type lands as a child node — qualified_name for dotted
; paths, identifier for single-segment cases.
(using_directive [(qualified_name) (identifier)] @import.module)
