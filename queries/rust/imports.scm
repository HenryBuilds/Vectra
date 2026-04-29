; Import / include extraction for Rust.

; "use foo::bar::Baz;" — capture the whole declaration; the path is
; nested and best surfaced verbatim rather than reconstructed here.
(use_declaration) @import.use

; "extern crate foo;" — legacy but still seen.
(extern_crate_declaration) @import.crate
