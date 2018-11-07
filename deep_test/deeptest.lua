local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_test"

local test_deep = true
local test_clonable = false

local performTest = function( obj_)
	obj_:set(666)
	print( "immediate:", obj_)

	l:set( "key", obj_)
	local out = l:get( "key")
	print( "out of linda:", out)

	local g = lanes.gen(
		"package"
		, {
			required = { "deep_test"} -- we will transfer userdata created by this module, so we need to make this lane aware of it
		}
		, function( obj_)
			print( "in lane:", obj_)
			return obj_
		end
	)
	h = g( obj_)
	local from_lane = h[1]
	print( "from lane:", from_lane)
end

if test_deep then
	performTest( dt.new_deep())
end

if test_clonable then
	performTest( dt.new_clonable())
end
