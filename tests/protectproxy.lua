local lanes = require "lanes".configure{ with_timers = false}

local body = function( param)
	print ( "lane body: " .. param)
	return 1
end

local gen = lanes.gen( "*", { name = 'auto' }, body)

local mylane = gen( "hello")

local result = mylane[1]

-- make sure we have properly protected the lane

-- can't access the metatable
print( "metatable:" .. tostring( getmetatable( mylane)))

-- can't write to the userdata
print( "lane result: " .. mylane[1])

-- read nonexistent values -> nil
print "reading nonexistent return value"
a = mylane[2]

print "writing to the lane -> error"
mylane[4] = true
