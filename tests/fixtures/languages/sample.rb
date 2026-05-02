# Minimal Ruby fixture for language parse-validation tests.

require "set"

module Demo
  class Greeter
    def initialize(label)
      @label = label
    end

    def greet
      "Hello, #{@label}"
    end
  end

  def self.add(a, b)
    a + b
  end
end
