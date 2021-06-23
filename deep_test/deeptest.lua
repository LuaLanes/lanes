local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_test"

local test_deep = false
local test_clonable = false
local test_clonable_as_upvalue = true

local performTest = function( obj_, uservalue_)
	-- setup the userdata with some value and a uservalue
	obj_:set( 666)
	obj_:setuv( 1, uservalue_)

	-- read back the contents of the object
	print( "immediate:", obj_, obj_:getuv( 1))

	-- send the object in a linda, get it back out, read the contents
	l:set( "key", obj_)
	local out = l:get( "key")
	print( "out of linda:", out, out:getuv( 1))

	-- send the object in a lane through parameter passing, the lane body returns it as return value, read the contents
	local g = lanes.gen(
		"package"
		, {
			required = { "deep_test"} -- we will transfer userdata created by this module, so we need to make this lane aware of it
		}
		, function( param_)
			-- read contents inside lane
			print( "in lane:", param_, param_:getuv( 1))
			return param_
		end
	)
	h = g( obj_)
	local from_lane = h[1]
	print( "from lane:", from_lane, from_lane:getuv( 1))
end

if test_deep then
	performTest( dt.new_deep(), "some uservalue")
end

if test_clonable then
	performTest( dt.new_clonable(), "my uservalue")
end

if test_clonable_as_upvalue then
	local clonable = dt.new_clonable()
	-- pull clonable as upvalue in a function
	local f = function()
		print( clonable)
	end
	-- set function as uservalue of clonable (thus, clonable is referenced as upvalue in its function uservalue)
	performTest( clonable, f)
end