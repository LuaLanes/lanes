local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "_assert"
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

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

local generator = lanes.gen("", { name = 'auto', globals={hey=true}, gc_cb = gc_cb }, task)

local N = 999999999
local lane_h = generator(1,N,1)   -- huuuuuuge...

-- Wait until state changes "pending"->"running"
--
local st
local t0 = os.time()
while os.time()-t0 < 5 do
    st = lane_h.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="pending" then break end
end
PRINT(" "..st)

if st == "error" then
    local _ = lane_h[0]  -- propagate the error here
end
if st == "done" then
    error("Looping to "..N.." was not long enough (cannot test cancellation)")
end
assert(st == "running", "st == " .. st)

-- when running under luajit, the function is JIT-ed, and the instruction count isn't hit, so we need a different hook
lane_h:cancel(jit and "line" or "count", 100) -- 0 timeout, hook triggers cancelslation when reaching the specified count

local t0 = os.time()
while os.time()-t0 < 5 do
    st = lane_h.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="running" then break end
end
PRINT(" "..st)
assert(st == "cancelled", "st is '" .. st .. "' instead of 'cancelled'")
