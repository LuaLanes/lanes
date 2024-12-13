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

PRINT("\n\n", "---=== Tasking (cancelling) ===---", "\n\n")

local task_launch2 = lanes_gen("", { globals={hey=true}, gc_cb = gc_cb}, task)

local N=999999999
local lane9= task_launch2(1,N,1)   -- huuuuuuge...

-- Wait until state changes "pending"->"running"
--
local st
local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="pending" then break end
end
PRINT(" "..st)

if st=="error" then
    local _= lane9[0]  -- propagate the error here
end
if st=="done" then
    error("Looping to "..N.." was not long enough (cannot test cancellation)")
end
assert(st=="running", "st == " .. st)

-- when running under luajit, the function is JIT-ed, and the instruction count isn't hit, so we need a different hook
lane9:cancel(jit and "line" or "count", 100) -- 0 timeout, hook triggers cancelslation when reaching the specified count

local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="running" then break end
end
PRINT(" "..st)
assert(st == "cancelled", "st is '" .. st .. "' instead of 'cancelled'")

-- cancellation of lanes waiting on a linda
local limited = lanes_linda("limited")
assert.fails(function() limited:limit("key", -1) end)
assert.failsnot(function() limited:limit("key", 1) end)
-- [[################################################
limited:send("key", "hello") -- saturate linda
for k, v in pairs(limited:dump()) do
    PRINT("limited[" .. tostring(k) .. "] = " .. tostring(v))
end
local wait_send = function()
    local a,b
    set_finalizer(function() print("wait_send", a, b) end)
    a,b = limited:send("key", "bybye") -- infinite timeout, returns only when lane is cancelled
end

local wait_send_lane = lanes.gen("*", wait_send)()
repeat until wait_send_lane.status == "waiting"
print "wait_send_lane is waiting"
wait_send_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_send_lane.status == "cancelled"
print "wait_send_lane is cancelled"
--################################################]]
local wait_receive = function()
    local k, v
    set_finalizer(function() print("wait_receive", k, v) end)
    k, v = limited:receive("dummy") -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_lane = lanes.gen("*", wait_receive)()
repeat until wait_receive_lane.status == "waiting"
print "wait_receive_lane is waiting"
wait_receive_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_lane.status == "cancelled"
print "wait_receive_lane is cancelled"
--################################################]]
local wait_receive_batched = function()
    local k, v1, v2
    set_finalizer(function() print("wait_receive_batched", k, v1, v2) end)
    k, v1, v2 = limited:receive(limited.batched, "dummy", 2) -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_batched_lane = lanes.gen("*", wait_receive_batched)()
repeat until wait_receive_batched_lane.status == "waiting"
print "wait_receive_batched_lane is waiting"
wait_receive_batched_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_batched_lane.status == "cancelled"
print "wait_receive_batched_lane is cancelled"
--################################################]]
