local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- a lane coroutine that yields back what got in, one element at a time
local yielder = function(...)
    local utils = lanes.require "_utils"
    local PRINT = utils.MAKE_PRINT()
    PRINT "In lane"
    for _i  = 1, select('#', ...) do
        local _val = select(_i, ...)
        PRINT("yielding #", _i, _val)
        local _ack = coroutine.yield(_val)
        assert(_ack == _i)
    end
    return "bye!"
end

--------------------------------------------------------------------------------------------------
-- TEST: if we start a non-coroutine lane with a yielding function, we should get an error, right?
--------------------------------------------------------------------------------------------------
if false then
    local fun_g = lanes.gen("*", { name = 'auto' }, yielder)
    local h = fun_g("hello", "world", "!")
    local err, status, stack = h:join()
    PRINT(err, status, stack)
    -- the actual error message is not the same for Lua 5.1
    -- of course, it also has to be different for LuaJIT as well
    -- also, LuaJIT prepends a file:line to the actual error message, which Lua5.1 does not.
    local msgs = {
        ["Lua 5.1"] = jit and "attempt to yield across C-call boundary" or "attempt to yield across metamethod/C-call boundary",
        ["Lua 5.2"] = "attempt to yield from outside a coroutine",
        ["Lua 5.3"] = "attempt to yield from outside a coroutine",
        ["Lua 5.4"] = "attempt to yield from outside a coroutine"
    }
    local expected_msg = msgs[_VERSION]
    PRINT("expected_msg = " .. expected_msg)
    assert(err == nil and string.find(status, expected_msg, 1, true) and stack == nil, "status = " .. status)
end

-- the coroutine generator
local coro_g = lanes.coro("*", {name = "auto"}, yielder)

-------------------------------------------------------------------------------------------------
-- TEST: we can resume as many times as the lane yields, then read the returned value on indexing
-------------------------------------------------------------------------------------------------
if false then
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
if false then
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

---------------------------------------------------------------------------------------------------
-- TEST: if we join a yielded lane, we get a timeout, and we can resume as if we didn't try to join
---------------------------------------------------------------------------------------------------
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

-----------------------------------------------------------------------
-- TEST: if we index yielded lane, we should get the last yielded value
-----------------------------------------------------------------------
if false then
    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the first yielded value, sending back the expected index
    assert(h:resume(1) == "hello")
    -- indexing multiple times gives back the same us the same yielded value
    local r1 = h[1]
    local r2 = h[1]
    local r3 = h[1]
    assert(r1 == "world" and r2 == "world" and r3 == "world", "got " .. r1 .. " " .. r2 .. " " .. r3)
    assert(h:resume(2) == "world")

    -- THERE IS AN INCONSISTENCY: h:resume pulls the yielded values directly out of the lane's stack
    -- but h[n] removes them and stores them in the internal values storage table
    -- TODO: so we need to decide: should indexing a yielded lane work like resume()?
    assert(h:resume(3) == "!")
end

-------------------------------------------------------------------------------------
-- TEST: if we close yielded lane, we can join it and get the last yielded values out
-------------------------------------------------------------------------------------
-- TODO: we need to implement lane:close() for that!
