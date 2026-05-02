; Import / include extraction for JSON.
;
; JSON has no native import concept. Manifests like package.json carry
; dependency objects whose keys ARE imports, but recognising that
; requires file-name awareness (package.json vs. tsconfig.json) which
; is a layer above the language registry. We intentionally capture
; nothing here.
