; Import / include extraction for Python.

; "import foo" / "import foo.bar"
(import_statement
  name: (dotted_name) @import.module)

; "from foo import x"
(import_from_statement
  module_name: (dotted_name) @import.module)

; "from . import x" / "from ..foo import x"
(import_from_statement
  module_name: (relative_import) @import.module)
