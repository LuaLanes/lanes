local lanes = require "lanes".configure{on_state_create = require "fixture".on_state_create}

-- launch lanes that cooperate properly with cancellation request

local lane1 = function()
	lane_threadname("lane1")
	-- loop breaks on soft cancellation request
	repeat
		lanes.sleep(0)
	until cancel_test()
	print "lane1 cancelled"
end

local lane2 = function(linda_)
	lane_threadname("lane2")
	-- loop breaks on lane/linda cancellation
	repeat
		local k, v = linda_:receive('k')
	until v == lanes.cancel_error
	print "lane2 cancelled"
end

local lane3 = function()
	lane_threadname("lane3")
	-- this one cooperates too, because of the hook cancellation modes that Lanes will be using
	local fixture = require "fixture"
	repeat until fixture.give_me_back(false)
end



-- the generators
local g1 = lanes.gen("*", { name = 'auto' }, lane1)
local g2 = lanes.gen("*", { name = 'auto' }, lane2)
local g3 = lanes.gen("*", { name = 'auto' }, lane3)

-- launch lanes
local h1 = g1()

local linda = lanes.linda()
local h2 = g2(linda)

local h3 = g3()

lanes.sleep(0.1)

local is_running = function(lane_h)
	local status = lane_h.status
	return status == "running" or status == "waiting"
end

-- wait until they are all started
repeat until is_running(h1) and is_running(h2) and is_running(h3)

-- let the script terminate, Lanes should not crash at shutdown
