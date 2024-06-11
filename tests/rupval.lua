lanes = require "lanes".configure{with_timers=false}

-- make sure we can copy functions with interdependant upvalues

local x = 33
local y = 44
local z = 55

local b
local a = function( n)
	print( "a", n)
	return n <= 0 and x or b( n-1)
end

local c
b = function( n)
	print( "b", n)
	return n <= 0 and y or c( n-1)
end

c = function( n)
	print( "c", n)
	return n <= 0 and z or a( n-1)
end

local g = lanes.gen( "base", a)

local l = g(7)
local r = l:join()
assert(r == y)
print(r)

local l = g(8)
local r = l:join()
assert(r == z)
print(r)

local l = g(9)
local r = l:join()
assert(r == x)
print(r)

print "TEST OK"
