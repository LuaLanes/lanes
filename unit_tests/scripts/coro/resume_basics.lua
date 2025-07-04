local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- the coroutine generator
local coro_g = lanes.coro("*", {name = "auto"}, utils.yield_one_by_one)

-------------------------------------------------------------------------------------------------
-- TEST: we can resume as many times as the lane yields, then read the returned value on indexing
-------------------------------------------------------------------------------------------------
if true then
    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the yielded values, sending back the expected index
    assert(h:resume(1) == "hello")
    assert(h:resume(2) == "world")
    assert(h:resume(3) == "!")
    -- the lane return value is available as usual
    local r = h[1]
    assert(r == "bye!")
end

---------------------------------------------------------------------------------------------
-- TEST: we can resume as many times as the lane yields, then read the returned value on join
---------------------------------------------------------------------------------------------
if true then
    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the yielded values, sending back the expected index
    assert(h:resume(1) == "hello")
    assert(h:resume(2) == "world")
    assert(h:resume(3) == "!")
    -- the lane return value is available as usual
    local s, r = h:join()
    assert(h.status == "done" and s == true and r == "bye!")
end
