; Symbol extraction for Dockerfile.
;
; The named entities in a Dockerfile are the multi-stage build
; aliases: `FROM debian:bookworm AS builder` declares "builder".
; The `as:` field is set when the AS clause is present; otherwise
; the FROM is anonymous and produces no symbol.
(from_instruction as: (image_alias) @symbol.type)
