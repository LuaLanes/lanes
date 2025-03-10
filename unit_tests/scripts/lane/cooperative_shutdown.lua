local lanes = require "lanes"

-- launch lanes that cooperate properly with cancellation request

local lane1 = function()
	lane_threadname("lane1")
	-- loop breaks on cancellation request
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
	-- but not with LuaJIT, because the function is compiled, and we don't call anyone, so no hook triggers
	local fixture = require "fixture"
	repeat until fixture.give_me_back(false)
end



-- the generators
local g1 = lanes.gen("*", lane1)
local g2 = lanes.gen("*", lane2)
local g3 = lanes.gen("*", lane3)

-- launch lanes
local h1 = g1()

local linda = lanes.linda()
local h2 = g2(linda)

local h3 = g3()

-- wait until they are both started
repeat until h1.status == "running" and h2.status == "waiting" and h3.status == "running"

-- let the script terminate, Lanes should not crash at shutdown
