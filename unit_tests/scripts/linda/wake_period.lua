-- default wake period is 0.5 seconds
local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure{linda_wake_period = 0.5}
local lanes = require_lanes_result_1

-- a lane that performs a blocking operation for 2 seconds
local body = function(linda_)
	-- a blocking read that lasts longer than the tested wake_period values
	linda_:receive(2, "empty_slot")
	return "done"
end

-- if we don't cancel the lane, we should wait the whole duration
local function check_wake_duration(linda_, expected_, do_cancel_)
	local h = lanes.gen(body)(linda_)
	-- wait until the linda is blocked
	repeat until h.status == "waiting"
	local t0 = lanes.now_secs()
	-- soft cancel, no timeout, no waking
	if do_cancel_ then
		local result, reason = h:cancel('soft', 0, false)
		-- should say there was a timeout, since the lane didn't actually cancel (normal since we did not wake it)
		assert(result == false and reason == 'timeout', "unexpected cancel result")
	end
	-- this should wait until the linda wakes by itself before the actual receive timeout and sees the cancel request
	local r, ret = h:join()
	assert(r == true and ret == "done")
	local t1 = lanes.now_secs()
	local delta = t1 - t0
	-- the linda should check for cancellation at about the expected period, not earlier
	assert(delta >= expected_, tostring(linda_) .. " woke too early:" .. delta)
	-- the lane shouldn't terminate too long after cancellation was processed
	assert(delta <= expected_ * 1.1, tostring(linda_) .. " woke too late: " .. delta)
end

-- legacy behavior: linda waits until operation timeout
check_wake_duration(lanes.linda{name = "A", wake_period = 'never'}, 2, true)
-- early wake behavior: linda wakes after the expected time, sees a cancellation requests, and aborts the operation early
check_wake_duration(lanes.linda{name = "B", wake_period = 0.25}, 0.25, true)
check_wake_duration(lanes.linda{name = "C"}, 0.5, true) -- wake_period defaults to 0.5 (see above)
check_wake_duration(lanes.linda{name = "D", wake_period = 1}, 1, true)
-- even when there is a wake_period, the operation should reach full timeout if not cancelled early
check_wake_duration(lanes.linda{name = "E", wake_period = 0.1}, 2, false)
