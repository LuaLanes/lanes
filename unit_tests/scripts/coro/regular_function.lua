local lanes = require "lanes".configure()

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- a lane body that just returns some value
local returner = function(msg_)
    local utils = lanes.require "_utils"
    local PRINT = utils.MAKE_PRINT()
    PRINT "In lane"
    assert(msg_ == "hi")
    return "bye"
end

-- a function that returns some value can run in a coroutine
if true then
    -- the generator
    local g = lanes.coro("*", {name = "auto"}, returner)

    -- launch lane
    local h = g("hi")

    local r = h[1]
    assert(r == "bye")
end

-- can't resume a coro after the lane body has returned
if true then
    -- the generator
    local g = lanes.coro("*", {name = "auto"}, returner)

    -- launch lane
    local h = g("hi")

    -- resuming a lane that terminated execution should raise an error
    local b, e = pcall(h.resume, h)
    assert(b == false and type(e) == "string")
end
