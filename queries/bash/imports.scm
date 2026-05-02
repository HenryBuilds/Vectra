; Import / include extraction for Bash.
;
; Shell has no static import keyword: scripts pull other files in via
; `source path/to/foo.sh` or the equivalent dot-builtin `. path/to/foo.sh`.
; Both expand to a `command` node whose name is `source` or `.`. We
; capture the first argument as the imported path.
(command
  name: (command_name (word) @_cmd)
  argument: [(word) (string) (raw_string)] @import.module
  (#match? @_cmd "^(source|\\.)$"))
