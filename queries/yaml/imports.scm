; Import / include extraction for YAML.
;
; YAML has no native import keyword. Specific document shapes encode
; "imports" in domain-specific ways — `uses:` in GitHub Actions,
; `extends:` in compose / GitLab CI, `include:` in some configs —
; but recognising them requires schema awareness above the language
; layer. We capture nothing here.
