// Minimal C++ fixture for language parse-validation tests.

namespace demo {

class Greeter {
public:
    explicit Greeter(int n) : n_(n) {}
    int value() const { return n_; }

private:
    int n_;
};

int free_function(int x) {
    return x + 1;
}

}  // namespace demo
