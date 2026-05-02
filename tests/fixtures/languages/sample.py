# Minimal Python fixture for language parse-validation tests.


def add(a, b):
    return a + b


class Greeter:
    def __init__(self, label):
        self.label = label

    def greet(self):
        return f"Hello, {self.label}"
