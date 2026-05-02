; Import / include extraction for TOML.
;
; TOML has no native include keyword. Manifests like Cargo.toml
; encode dependencies inside specific tables (`[dependencies]`),
; but recognising those requires schema awareness above the
; language layer. We capture nothing here.
