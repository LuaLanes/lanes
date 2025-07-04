local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- the coroutine generator
local coro_g = lanes.coro("*", {name = "auto"}, utils.yield_one_by_one)

---------------------------------------------------
-- TEST: if we join a yielded lane, the lane aborts
---------------------------------------------------
if true then
    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the first yielded value, sending back the expected index
    assert(h:resume(1) == "hello")
    -- join the lane. since it will reach a yield point, it unblocks and ends. last yielded values are returned normally
    local b, r = h:join(0.5)
    local s = h.status
    assert(s == "done" and b == true and r == "world", "got " .. s .. " " .. tostring(b) .. " " .. tostring(r))
end
