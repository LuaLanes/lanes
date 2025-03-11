local lanes = require("lanes").configure{ with_timers = false}
local l = lanes.linda "my linda"

local table_unpack = table.unpack or unpack -- Lua 5.1 support

-- we will transfer userdata created by this module, so we need to make Lanes aware of it
local dt = lanes.require "deep_userdata_example"

-- set DEEP to any non-false value to run the Deep Userdata tests. "gc" selects a special test for debug purposes
DEEP = DEEP or true
-- set CLONABLE to any non-false value to run the Clonable Userdata tests
CLONABLE = CLONABLE or true

-- lua 5.1->5.2 support a single table uservalue
-- lua 5.3->5.4 supports an arbitrary type uservalue
local test_uvtype = (_VERSION == "Lua 5.4") and "function" or (_VERSION == "Lua 5.3") and "string" or "table"
-- lua 5.4 supports multiple uservalues
local nupvals = _VERSION == "Lua 5.4" and 3 or 1

local makeUserValue = function( obj_)
	if test_uvtype == "table" then
		return {"some uservalue"}
	elseif test_uvtype == "string" then
		return "some uservalue"
	elseif test_uvtype == "function" then
		-- a function that pull the userdata as upvalue
		local f = function()
			return "-> '" .. tostring( obj_) .. "'"
		end
		return f
	end
end

local printDeep = function( prefix_, obj_, t_)
	print( prefix_, obj_)
	for uvi = 1, nupvals do
		local uservalue = obj_:getuv(uvi)
		print ("uv #" .. uvi, type( uservalue), uservalue, type(uservalue) == "function" and uservalue() or "")
	end
	if t_ then
		local count = 0
		for k, v in ipairs( t_) do
			print( "t["..tostring(k).."]", v)
			count = count + 1
		end
		-- we should have only 2 indexed entries with the same value
		assert(count == 2 and t_[1] == t_[2])
	end
	print()
end

local performTest = function( obj_)
	-- setup the userdata with some value and a uservalue
	obj_:set( 666)
	obj_:setuv( 1, makeUserValue( obj_))
	if nupvals > 1 then
		-- keep uv #2 as nil
		obj_:setuv( 3, "ENDUV")
	end

	local t =
	{
		-- two indices with an identical value: we should also have identical values on the other side (even if not the same as the original ones when they are clonables)
		obj_,
		obj_,
		-- this one won't transfer because we don't support full uservalue as keys
		[obj_] = "val"
	}

	-- read back the contents of the object
	printDeep( "immediate:", obj_, t)

	-- send the object in a linda, get it back out, read the contents
	l:set( "key", obj_, t)
	-- when obj_ is a deep userdata, out is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	local _n, _val1, _val2 = l:get( "key", 2)
	assert(_n == (_val2 and 2 or 1))
	printDeep( "out of linda:", _val1, _val2)

	-- send the object in a lane through argument passing, the lane body returns it as return value, read the contents
	local g = lanes.gen(
		"package"
		, {
			required = { "deep_userdata_example"} -- we will transfer userdata created by this module, so we need to make this lane aware of it
		}
		, function( arg_, t_)
			-- read contents inside lane: arg_ and t_ by argument
			printDeep( "in lane, as arguments:", arg_, t_)
			-- read contents inside lane: obj_ and t by upvalue
			printDeep( "in lane, as upvalues:", obj_, t)
			-- read contents inside lane: in linda
			local _n, _val1, _val2 = l:get( "key", 2)
			assert(_n == (_val2 and 2 or 1))
			printDeep( "in lane, from linda:", _val1, _val2)
			return arg_, t_
		end
	)
	h = g( obj_, t)
	-- when obj_ is a deep userdata, from_lane is the same userdata as obj_ (not another one pointing on the same deep memory block) because of an internal cache table [deep*] -> proxy)
	-- when obj_ is a clonable userdata, we get a different clone everytime we cross a linda or lane barrier
	printDeep( "from lane:", h[1], h[2])
end

if DEEP then
	print "================================================================"
	print "DEEP"
	local d = dt.new_deep(nupvals)
	if type(DEEP) == "string" then
		local gc_tests = {
			thrasher = function(repeat_, size_)
				print "in thrasher"
				-- result is a table of repeat_ tables, each containing size_ entries
				local result = {}
				for i = 1, repeat_ do
					local batch_values = {}
					for j = 1, size_ do
						table.insert(batch_values, j)
					end
					table.insert(result, batch_values)
				end
				print "thrasher done"
				return result
			end,
			stack_abuser = function(repeat_, size_)
				print "in stack_abuser"
				for i = 1, repeat_ do
					local batch_values = {}
					for j = 1, size_ do
						table.insert(batch_values, j)
					end
					-- return size_ values
					local _ = table_unpack(batch_values)
				end
				print "stack_abuser done"
				return result
			end
		}
		-- have the object call the function from inside one of its functions, to detect if it gets collected from there (while in use!)
		local testf = gc_tests[DEEP]
		if testf then
			local r = d:invoke(gc_tests[DEEP], REPEAT or 10, SIZE or 10)
			print("invoke -> ", tostring(r))
		else
			print("unknown test '" .. DEEP .. "'")
		end
	else
		performTest(d)
	end
end

if CLONABLE then
	print "================================================================"
	print "CLONABLE"
	performTest( dt.new_clonable(nupvals))
end

print "================================================================"
print "TEST OK"