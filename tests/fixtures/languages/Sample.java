// Minimal Java fixture for language parse-validation tests.

package demo;

import java.util.List;

public class Sample {
    private final String label;

    public Sample(String label) {
        this.label = label;
    }

    public String greet() {
        return "Hello, " + label;
    }

    public static int add(int a, int b) {
        return a + b;
    }
}

interface Doubler {
    int doubled();
}
