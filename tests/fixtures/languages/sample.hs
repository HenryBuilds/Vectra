-- Minimal Haskell fixture for language parse-validation tests.

module Demo where

import Data.List (sort)

data Greeter = Greeter { label :: String }

greet :: Greeter -> String
greet g = "Hello, " ++ label g

add :: Int -> Int -> Int
add a b = a + b
