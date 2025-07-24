local lanes = require "lanes"

-- a newly created linda doesn't contain anything
local l = lanes.linda()

-- send a function and a string, make sure that's what we read back
l:send("k", function() end, "str")
local c = l:count("k")
assert(c == 2, "got " .. c)
local k, v1, v2 = l:receive_batched("k", 2)
local tv1, tv2 = type(v1), type(v2)
assert(k == "k" and tv1 == "function" and tv2 == "string", "got " .. tv1 .. " " .. tv2)
assert(l:count("k") == 0)
