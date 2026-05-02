# Minimal Elixir fixture for language parse-validation tests.

defmodule Demo.Greeter do
  @moduledoc "Tiny greeter for fixture purposes."

  alias String.Chars

  def greet(label) do
    "Hello, " <> label
  end

  defp internal(value), do: value
end

defmodule Demo.Util do
  def add(a, b), do: a + b
end
