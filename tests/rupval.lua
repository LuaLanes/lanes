lanes = require "lanes".configure{with_timers=false}

-- make sure we can copy functions with interdependant upvalues

local b
local a = function( n)
  print( "a", n)
	return n <= 0 and n or b( n-1)
end

local c
b = function( n)
	print( "b", n)
	return n <= 0 and n or c( n-1)
end

c = function( n)
	print( "c", n)
	return n <= 0 and n or a( n-1)
end

local g = lanes.gen( "*", a)

local l = g(10)
l:join()
