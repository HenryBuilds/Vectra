#!/usr/bin/env bash
# Minimal Bash fixture for language parse-validation tests.

source ./helpers.sh

greet() {
    local label="$1"
    echo "Hello, ${label}"
}

add() {
    local a="$1"
    local b="$2"
    echo $((a + b))
}

greet "world"
