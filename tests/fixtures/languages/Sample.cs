// Minimal C# fixture for language parse-validation tests.

using System;

namespace Demo;

public class Greeter
{
    private readonly string _label;

    public Greeter(string label)
    {
        _label = label;
    }

    public string Greet() => $"Hello, {_label}";
}

public static class Util
{
    public static int Add(int a, int b) => a + b;
}

public interface IDoubler
{
    int Doubled();
}
