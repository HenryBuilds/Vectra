// Minimal Dart fixture for language parse-validation tests.

import 'dart:async';

class Greeter {
  final String label;

  Greeter(this.label);

  String greet() => 'Hello, $label';
}

mixin Doubler {
  int doubled();
}

enum Status { active, idle, done }

int add(int a, int b) {
  return a + b;
}
