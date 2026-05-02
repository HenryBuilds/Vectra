// Minimal Kotlin fixture for language parse-validation tests.

package demo

import kotlin.collections.List

class Greeter(val label: String) {
    fun greet(): String {
        return "Hello, $label"
    }
}

object Util {
    fun add(a: Int, b: Int): Int = a + b
}

fun square(x: Int): Int = x * x
