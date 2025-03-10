local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

if true then
    -- a lane body that just returns some value
    local lane = function(msg_)
        local utils = lanes.require "_utils"
        local PRINT = utils.MAKE_PRINT()
        PRINT "In lane"
        assert(msg_ == "hi")
        return "bye"
    end

    -- the generator
    local g1 = lanes.coro("*", {name = "auto"}, lane)

    -- launch lane
    local h1 = g1("hi")

    local r = h1[1]
    assert(r == "bye")
end

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
    return "done!"
end

if true then
    -- if we start a non-coroutine lane with a yielding function, we should get an error, right?
    local fun_g = lanes.gen("*", {name = "auto"}, yielder)
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

-- the generator
local coro_g = lanes.coro("*", {name = "auto"}, yielder)

if true then
    -- launch coroutine lane
    local h2 = coro_g("hello", "world", "!")
    -- read the yielded values, sending back the expected index
    assert(h2:resume(1) == "hello")
    assert(h2:resume(2) == "world")
    assert(h2:resume(3) == "!")
    -- the lane return value is available as usual
    local r = h2[1]
    assert(r == "done!")
end

if true then
    -- another coroutine lane
    local h3 = coro_g("hello", "world", "!")

    -- yielded values are available as regular return values
    assert(h3[1] == "hello" and h3.status == "suspended")
    -- since we consumed the returned values, they should not be here when we resume
    assert(h3:resume(1) == nil)

    -- similarly, we can get them with join()
    assert(h3:join() == "world" and h3.status == "suspended")
    -- since we consumed the returned values, they should not be here when we resume
    assert(h3:resume(2) == nil)

    -- the rest should work as usual
    assert(h3:resume(3) == "!")

    -- the final return value of the lane body remains to be read
    assert(h3:join() == "done!" and h3.status == "done")
end
