# Minimal R fixture for language parse-validation tests.

library(stats)
library(utils)

greet <- function(label) {
  paste0("Hello, ", label)
}

add <- function(a, b) {
  a + b
}

square <- function(x) x * x
