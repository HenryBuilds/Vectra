// Minimal Go fixture for language parse-validation tests.

package demo

import "fmt"

type Greeter struct {
	Label string
}

func (g *Greeter) Greet() string {
	return fmt.Sprintf("Hello, %s", g.Label)
}

func Add(a, b int) int {
	return a + b
}
