local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "_assert"
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

local lanes_gen = assert(lanes.gen)
local lanes_linda = assert(lanes.linda)

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

local SLEEP = function(...)
    local k, v = lanes.sleep(...)
    assert(k == nil and v == "timeout")
end

PRINT("---=== :join test ===---", "\n\n")

-- NOTE: 'unpack()' cannot be used on the lane handle; it will always return nil
--       (unless [1..n] has been read earlier, in which case it would seemingly
--       work).

local S= lanes_gen("table", {gc_cb = gc_cb},
    function(arg)
        lane_threadname "join test lane"
        set_finalizer(function() end)
        local aux= {}
        for i, v in ipairs(arg) do
           table.insert(aux, 1, v)
        end
        -- unpack was renamed table.unpack in Lua 5.2: cater for both!
    return (unpack or table.unpack)(aux)
end)

h= S { 12, 13, 14 }     -- execution starts, h[1..3] will get the return values
-- wait a bit so that the lane has a chance to set its debug name
SLEEP(0.5)
print("joining with '" .. h:get_threadname() .. "'")
local a,b,c,d= h:join()
if h.status == "error" then
    print(h:get_threadname(), "error: " , a, b, c, d)
else
    print(h:get_threadname(), a,b,c,d)
    assert(a==14, "a == " .. tostring(a))
    assert(b==13, "b == " .. tostring(b))
    assert(c==12, "c == " .. tostring(c))
    assert(d==nil, "d == " .. tostring(d))
end

local nameof_type, nameof_name = lanes.nameof(print)
PRINT("name of " .. nameof_type .. " print = '" .. nameof_name .. "'")
