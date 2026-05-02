; Import / include extraction for Dart.
;
; Dart's `import 'package:foo/bar.dart' as f show baz;` reduces to
; a `library_import` whose only required child is `import_specification`
; — that's where the URL string and modifiers live.
(library_import (import_specification) @import.module)
