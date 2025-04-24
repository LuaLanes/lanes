local require_fixture_result_1, require_fixture_result_2 = require "fixture"
local fixture = assert(require_fixture_result_1)

local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure{on_state_create = fixture.on_state_create}.configure()
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

-- cancellation of cooperating lanes
local cooperative = function()
    local fixture = assert(require "fixture")
    local which_cancel
    repeat
        fixture.block_for(0.2)
        which_cancel = cancel_test()
    until which_cancel
    return which_cancel
end
-- soft and hard are behaviorally equivalent when no blocking linda operation is involved
local cooperative_lane_soft = lanes_gen("*", { name = 'auto' }, cooperative)()
local a, b = cooperative_lane_soft:cancel("soft", 0) -- issue request, do not wait for lane to terminate
assert(a == false and b == "timeout", "got " .. tostring(a) .. " " .. tostring(b))
assert(cooperative_lane_soft[1] == "soft") -- return value of the lane body is the value returned by cancel_test()
local cooperative_lane_hard = lanes_gen("*", { name = 'auto' }, cooperative)()
local c, d = cooperative_lane_hard:cancel("hard", 0) -- issue request, do not wait for lane to terminate
assert(a == false and b == "timeout", "got " .. tostring(c) .. " " .. tostring(d))
assert(cooperative_lane_hard[1] == "hard") -- return value of the lane body is the value returned by cancel_test()

-- ##################################################################################################

-- cancellation of lanes waiting on a linda
local limited = lanes_linda{name = "limited"}
assert.fails(function() limited:limit("key", -1) end)
assert.failsnot(function() limited:limit("key", 1) end)
-- [[################################################
limited:send("key", "hello") -- saturate linda, so that subsequent sends will block
for k, v in pairs(limited:dump()) do
    PRINT("limited[" .. tostring(k) .. "] = " .. tostring(v))
end
local wait_send = function()
    local a,b
    set_finalizer(function() print("wait_send", a, b) end)
    a,b = limited:send("key", "bybye") -- infinite timeout, returns only when lane is cancelled
end

local wait_send_lane = lanes_gen("*", { name = 'auto' }, wait_send)()
repeat
    io.stderr:write('!')
    -- currently mingw64 builds can deadlock if we cancel the lane too early (before the linda blocks, at it causes the linda condvar not to be signalled)
    lanes.sleep(0.1)
until wait_send_lane.status == "waiting"
PRINT "wait_send_lane is waiting"
wait_send_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_send_lane.status == "cancelled"
PRINT "wait_send_lane is cancelled"
--################################################]]
local wait_receive = function()
    local k, v
    set_finalizer(function() print("wait_receive", k, v) end)
    k, v = limited:receive("dummy") -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_lane = lanes_gen("*", { name = 'auto' }, wait_receive)()
repeat
    io.stderr:write('!')
    -- currently mingw64 builds can deadlock if we cancel the lane too early (before the linda blocks, at it causes the linda condvar not to be signalled)
    lanes.sleep(0.1)
until wait_receive_lane.status == "waiting"
PRINT "wait_receive_lane is waiting"
wait_receive_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_lane.status == "cancelled"
PRINT "wait_receive_lane is cancelled"
--################################################]]
local wait_receive_batched = function()
    local k, v1, v2
    set_finalizer(function() print("wait_receive_batched", k, v1, v2) end)
    k, v1, v2 = limited:receive_batched("dummy", 2) -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_batched_lane = lanes_gen("*", { name = 'auto' }, wait_receive_batched)()
repeat
    io.stderr:write('!')
    -- currently mingw64 builds can deadlock if we cancel the lane too early (before the linda blocks, at it causes the linda condvar not to be signalled)
    lanes.sleep(0.1)
until wait_receive_batched_lane.status == "waiting"
PRINT "wait_receive_batched_lane is waiting"
wait_receive_batched_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_batched_lane.status == "cancelled"
PRINT "wait_receive_batched_lane is cancelled"
--################################################]]
