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

local function task(a, b, c)
    lane_threadname("task("..a..","..b..","..c..")")
    --error "111"     -- testing error messages
    assert(hey)
    local v=0
    for i=a,b,c do
        v= v+i
    end
    return v, hey
end

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Stdlib naming ===---", "\n\n")

local function dump_g(_x)
    lane_threadname "dump_g"
    assert(print)
    print("### dumping _G for '" .. _x .. "'")
    for k, v in pairs(_G) do
        print("\t" .. k .. ": " .. type(v))
    end
    return true
end

local function io_os_f(_x)
    lane_threadname "io_os_f"
    assert(print)
    print("### checking io and os libs existence for '" .. _x .. "'")
    assert(io)
    assert(os)
    return true
end

local function coro_f(_x)
    lane_threadname "coro_f"
    assert(print)
    print("### checking coroutine lib existence for '" .. _x .. "'")
    assert(coroutine)
    return true
end

assert.fails(function() lanes_gen("xxx", {gc_cb = gc_cb}, io_os_f) end)

local stdlib_naming_tests =
{
    -- { "", dump_g},
    -- { "coroutine", dump_g},
    -- { "io", dump_g},
    -- { "bit32", dump_g},
    { "coroutine?", coro_f}, -- in Lua 5.1, the coroutine base library doesn't exist (coroutine table is created when 'base' is opened)
    { "*", io_os_f},
    { "io,os", io_os_f},
    { "io+os", io_os_f},
    { "/io;os[base{", io_os_f}, -- use unconventional name separators to check that everything works fine
}

for _, t in ipairs(stdlib_naming_tests) do
    local f= lanes_gen(t[1], {gc_cb = gc_cb}, t[2])     -- any delimiter will do
    assert(f(t[1])[1])
end

PRINT("collectgarbage")
collectgarbage()
