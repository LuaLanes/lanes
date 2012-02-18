local lanes = require "lanes"
lanes.configure()

-- create a free-running lane

local linda = lanes.linda()

local f = function( _linda)
	_linda:receive("oy")
end

local g = function()
	local cancelled
	repeat
		cancelled = cancel_test()
	until cancelled
	print "User cancellation detected!"
end

local genF = lanes.gen( "", {globals = {threadName = "mylane"}}, f)
local genG = lanes.gen( "", g)


-- launch a good batch of free running lanes
for i = 1, 10 do
	-- if i%50 == 0 then print( i) end
	local h = genF( linda)
	local status
	repeat
		status = h.status
		--print( status)
	until status == "waiting"

	-- [[
	local h = genG()
	local status
	repeat
		status = h.status
		--print( status)
	until status == "running"
	--]]
end

print "exiting"

-- let process end terminate them and see what happens