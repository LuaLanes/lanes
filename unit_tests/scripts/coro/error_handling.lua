local lanes = require "lanes".configure{strip_functions = true}

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
        assert(_ack == _i, "unexpected reply ".._ack)
    end
    return "done!"
end

local force_error_test = function(error_trace_level_)
    -- the generator, minimal error handling
    local coro_g = lanes.coro("*", {name = "auto", error_trace_level = error_trace_level_}, yielder)

    -- launch coroutine lane
    local h = coro_g("hello", "world", "!")
    -- read the yielded values, sending back the expected index
    assert(h:resume(1) == "hello")
    assert(h:resume(2) == "world")
    -- mistake: we resume with 0 when the lane expects 3 -> assert() in the lane body!
    assert(h:resume(0) == "!")
    local a, b, c = h:join()
    PRINT(error_trace_level_ .. ":", a, b, c)
    local expected_c_type = error_trace_level_ == "minimal" and "nil" or "table"
    assert(h.status == "error" and string.find(b, "unexpected reply 0", 1, true) and type(c) == expected_c_type, "error message is " .. b)
    utils.dump_error_stack(error_trace_level_, c)
end

if false then
    force_error_test("minimal")
end

if false then
    force_error_test("basic")
end

if false then
    force_error_test("extended")
end

if true then
    -- start a coroutine lane that ends with a non-string error
    local non_string_thrower = function()
        error({"string in table"})
    end
    local coro_g = lanes.coro("*", {name = "auto"}, non_string_thrower)
    local h = coro_g()
    local a, b, c = h:join()
    -- we get the expected error back
    PRINT("non_string_thrower:", a, b, c)
    assert(a == nil and type(b) == "table" and b[1] == "string in table" and c == nil, "a=" .. tostring(a) .. " b=" .. tostring(b))
end
