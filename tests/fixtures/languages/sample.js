// Minimal JavaScript fixture for language parse-validation tests.

function add(a, b) {
    return a + b;
}

class Greeter {
    constructor(label) {
        this.label = label;
    }

    greet() {
        return `Hello, ${this.label}`;
    }
}

const square = (x) => x * x;
