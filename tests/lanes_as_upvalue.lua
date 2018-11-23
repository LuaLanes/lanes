local lanes = require "lanes".configure{ verbose_errors = true} -- with timers enabled

local function foo()
	local lanes = lanes -- lanes as upvalue
end

local g = lanes.gen( "*", foo)

-- this should raise an error as lanes.timer_lane is a Lane (a non-deep full userdata)
local res, err = pcall( g)
print( "Generating lane yielded: ", tostring( res), tostring( err))
