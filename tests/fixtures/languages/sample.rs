// Minimal Rust fixture for language parse-validation tests.

pub struct Greeter {
    label: String,
}

impl Greeter {
    pub fn new(label: &str) -> Self {
        Greeter {
            label: label.to_string(),
        }
    }

    pub fn greet(&self) -> String {
        format!("Hello, {}", self.label)
    }
}

pub fn add(a: i32, b: i32) -> i32 {
    a + b
}

pub trait Doubler {
    fn double(&self) -> Self;
}
