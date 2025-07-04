local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- the coroutine generator
local coro_g = lanes.coro("*", {name = "auto"}, utils.yield_one_by_one)

-------------------------------------------------------------------------
-- TEST: if we index a yielded lane, we should get the last yielded value
-------------------------------------------------------------------------
if true then
    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the first yielded value, sending back the expected index
    assert(h:resume(1) == "hello")
    -- indexing multiple times gives back the same us the same yielded value
    local r1 = h[1]
    local r2 = h[1]
    local r3 = h[1]
    assert(r1 == "world" and r2 == "world" and r3 == "world", "got " .. r1 .. " " .. r2 .. " " .. r3)
    -- once the lane was indexed, it is no longer resumable (just like after join)
    local b, e = pcall(h.resume, h, 2)
    assert(b == false and e == "cannot resume non-suspended coroutine Lane")
end
