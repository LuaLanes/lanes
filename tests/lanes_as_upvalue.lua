local lanes = require "lanes".configure() -- with timers enabled

local function foo()
	local lanes = lanes -- lanes as upvalue
end

local h = lanes.gen( "*", foo)()
h:join()
