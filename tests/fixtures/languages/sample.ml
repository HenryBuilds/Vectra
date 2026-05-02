(* Minimal OCaml fixture for language parse-validation tests. *)

open Printf

module Greeter = struct
  type t = { label : string }

  let make label = { label }

  let greet g = sprintf "Hello, %s" g.label
end

let add a b = a + b

let () = print_endline (Greeter.greet (Greeter.make "world"))
