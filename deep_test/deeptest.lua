local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_test"

local test_deep = true
local test_clonable = true
local test_uvtype = "string"
local nupvals = _VERSION == "Lua 5.4" and 2 or 1

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
	print( prefix_, obj_)
	for uvi = 1, nupvals do
		local uservalue = obj_:getuv( 1)
		print ( "uv #" .. uvi, uservalue, type( uservalue) == "function" and uservalue() or "")
	end
	if t_ then
		for k, v in pairs( t_) do
			print( "t["..tostring(k).."]", v)
		end
	end
	print()
end

local performTest = function( obj_)
	-- setup the userdata with some value and a uservalue
	obj_:set( 666)
	-- lua 5.1->5.2 support a single table uservalue
	-- lua 5.3 supports an arbitrary type uservalue
	obj_:setuv( 1, makeUserValue( obj_))
	-- lua 5.4 supports multiple uservalues of arbitrary types
	if nupvals > 1 then
		obj_:setuv( 2, "ENDUV")
	end

	local t =
	{
		["key"] = obj_,
		[obj_] = "val" -- this one won't transfer because we don't support full uservalue as keys
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
			-- read contents inside lane: in linda
			printDeep( "in lane, from linda:", l:get("key", 2))
			return arg_, t_
		end
	)
	h = g( obj_, t)
	-- when obj_ is a deep userdata, from_lane is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "from lane:", h[1], h[2])
end

if test_deep then
	print "================================================================"
	print "DEEP"
	performTest( dt.new_deep(nupvals))
end

if test_clonable then
	print "================================================================"
	print "CLONABLE"
	performTest( dt.new_clonable(nupvals))
end
