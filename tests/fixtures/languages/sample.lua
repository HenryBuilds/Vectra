-- Minimal Lua fixture for language parse-validation tests.

local M = {}
local socket = require("socket")

function M.greet(label)
    return "Hello, " .. label
end

function M.add(a, b)
    return a + b
end

return M
