; Import / include extraction for C++.

(preproc_include
  path: (string_literal) @import.local)

(preproc_include
  path: (system_lib_string) @import.system)

; "using namespace foo;" and "using foo::bar;" both create a name
; binding worth surfacing as an "import-like" relationship.
(using_declaration) @import.using
