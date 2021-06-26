local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_test"

local test_deep = true
local test_clonable = true
local test_uvtype = "function"

local makeUserValue = function( obj_)
	if test_uvtype == "string" then
		return "some uservalue"
	elseif test_uvtype == "function" then
		-- a function that pull the userdata as upvalue
		local f = function()
			return tostring( obj_)
		end
		return f
	end
end

local printDeep = function( prefix_, obj_, t_)
	local uservalue = obj_:getuv( 1)
	print( prefix_)
	print ( obj_, uservalue, type( uservalue) == "function" and uservalue() or "")
	if t_ then
		for k, v in pairs( t_) do
			print( k, v)
		end
	end
end

local performTest = function( obj_)
	-- setup the userdata with some value and a uservalue
	obj_:set( 666)
	-- lua 5.1->5.2 support a single table uservalue
	-- lua 5.3 supports an arbitrary type uservalue
	obj_:setuv( 1, makeUserValue( obj_))
	-- lua 5.4 supports multiple uservalues of arbitrary types
	-- obj_:setuv( 2, "ENDUV")

	local t =
	{
		["key"] = obj_,
		-- [obj_] = "val"
	}

	-- read back the contents of the object
	printDeep( "immediate:", obj_, t)

	-- send the object in a linda, get it back out, read the contents
	l:set( "key", obj_, t)
	-- when obj_ is a deep userdata, out is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "out of linda:", l:get( "key", 2))

	-- send the object in a lane through parameter passing, the lane body returns it as return value, read the contents
	local g = lanes.gen(
		"package"
		, {
			required = { "deep_test"} -- we will transfer userdata created by this module, so we need to make this lane aware of it
		}
		, function( arg_, t_)
			-- read contents inside lane: arg_ and t_ by argument
			printDeep( "in lane, as arguments:", arg_, t_)
			-- read contents inside lane: obj_ and t by upvalue
			printDeep( "in lane, as upvalues:", obj_, t)
			return arg_, t_
		end
	)
	h = g( obj_, t)
	-- when obj_ is a deep userdata, from_lane is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "from lane:", h[1], h[2])
end

if test_deep then
	print "DEEP"
	performTest( dt.new_deep())
end

if test_clonable then
	print "CLONABLE"
	performTest( dt.new_clonable())
end
