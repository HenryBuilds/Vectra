// Minimal Scala fixture for language parse-validation tests.

package demo

import scala.collection.mutable.Map

trait Doubler {
  def doubled: Int
}

class Greeter(label: String) {
  def greet(): String = s"Hello, $label"
}

object Util {
  def add(a: Int, b: Int): Int = a + b
}
