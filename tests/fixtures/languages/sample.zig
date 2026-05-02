// Minimal Zig fixture for language parse-validation tests.

const std = @import("std");

pub const Greeter = struct {
    label: []const u8,

    pub fn greet(self: Greeter, writer: anytype) !void {
        try writer.print("Hello, {s}\n", .{self.label});
    }
};

pub fn add(a: i32, b: i32) i32 {
    return a + b;
}

test "add works" {
    try std.testing.expectEqual(@as(i32, 3), add(1, 2));
}
