<?php
// Minimal PHP fixture for language parse-validation tests.

namespace Demo;

use SplStack;

class Greeter {
    public function __construct(private string $label) {}

    public function greet(): string {
        return "Hello, {$this->label}";
    }
}

interface Doubler {
    public function doubled(): int;
}

function add(int $a, int $b): int {
    return $a + $b;
}
