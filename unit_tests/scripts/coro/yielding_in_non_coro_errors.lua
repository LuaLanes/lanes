local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

--------------------------------------------------------------------------------------------------
-- TEST: if we start a non-coroutine lane with a yielding function, we should get an error, right?
--------------------------------------------------------------------------------------------------
local fun_g = lanes.gen("*", { name = 'auto' }, utils.yield_one_by_one)
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
    ["Lua 5.4"] = "attempt to yield from outside a coroutine",
    ["Lua 5.5"] = "attempt to yield from outside a coroutine"
}
local expected_msg = msgs[_VERSION]
PRINT("expected_msg = " .. expected_msg)
assert(err == nil and string.find(status, expected_msg, 1, true) and stack == nil, "status = " .. status)
