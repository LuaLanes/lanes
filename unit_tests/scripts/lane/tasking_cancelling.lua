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
