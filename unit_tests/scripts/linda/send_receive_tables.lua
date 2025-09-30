local lanes = require "lanes"
local dt = lanes.require "deep_userdata_example"

local l = lanes.linda()

-- =================================================================================================
-- send a table with subtables, both as keys and values, making sure the structure is preserved
-- =================================================================================================

if true then
	local t1 = {["name"] = "t1"}
	local t2 = {["name"] = "t2"}

	local t3 = {
		["name"] = "t3",
		["t1"] = t1,
		[t1] = t2, -- table t1 as key, table t2 as value
		["t2"] = t2,
		[t2] = t1 -- table t2 as key, table t1 as value
	}

	-- add recursivity for good measure
	t3["t3"] = t3
	t3[t3] = t3
	t1["t3"] = t3
	t2["t3"] = t3

	l:send("data", t3)
	local k, t = l:receive("data")
	assert(k == "data" and type(t) == "table" and t.name == "t3")
	-- when keys are strings
	assert(t["t3"] == t)
	assert(type(t["t1"]) == "table")
	assert(t["t1"].name == "t1")
	assert(type(t["t2"]) == "table")
	assert(t["t2"].name == "t2")
	assert(t["t1"].t3 == t)
	assert(t["t2"].t3 == t)
	-- when keys are tables
	assert(t[t.t1] == t.t2)
	assert(t[t.t1]["t3"] == t)
	assert(t[t.t2] == t.t1)
	assert(t[t.t2]["t3"] == t)
	assert(t[t.t3] == t.t3)
end

-- =================================================================================================
-- send a table with deep userdata keys and values
-- =================================================================================================

if true then
	local fixture = assert(require "fixture")
	local u = assert(fixture.newuserdata())
	assert(type(u) == "userdata")

	-- send a table where full userdata is used as key
	-- should fail because the userdata is not deep
	local s, r = pcall(l.send, l, "data", {["k"] = u})
	assert(s == false and type(r) == "string", "got " .. r)

	-- trying again with full userdata
	local d1 = dt.new_deep()
	assert(type(d1) == "userdata")
	local d2 = dt.new_deep()
	assert(type(d2) == "userdata")

	local t4 =
	{
		[d1] = d2,
		[d2] = d1
	}

	l:send("data", t4)
	local k, t = l:receive("data")
	assert(k == "data" and type(t) == "table")
	-- we should have 2 userdata entries in the table
	local count = 0
	for k, v in pairs(t) do
		assert(type(k) == "userdata")
		assert(type(v) == "userdata")
		count = count + 1
	end
	assert(count == 2, "got " .. count)
	-- all userdata identities should be preserved
	assert(type(t[d1]) == "userdata")
	assert(type(t[d2]) == "userdata")
	assert(t[d1] == d2)
	assert(t[d2] == d1)
	assert(t[d1] == t4[d1])
	assert(t[d2] == t4[d2])
end

-- =================================================================================================
-- send a table with clonable userdata keys and values
-- =================================================================================================

if true then
	local c1 = dt.new_clonable()
	c1:set(1)
	assert(type(c1) == "userdata")
	local c2 = dt.new_clonable()
	c2:set(2)
	assert(type(c2) == "userdata")

	l:send("data", c1)
	local k, c = l:receive("data")
	assert(k == "data" and type(c) == "userdata" and c:get() == 1, "got " .. tostring(c))

	local t5 =
	{
		[c1] = c2,
		[c2] = c1
	}

	l:send("data", t5)
	local k, t = l:receive("data")
	assert(k == "data" and type(t) == "table")
	-- all userdata identities should NOT be preserved, because the cloned userdatas in t are not the originals that were stored in t5
	assert(type(t[c1]) == "nil")
	assert(type(t[c2]) == "nil")
	-- but we should still have 2 userdata entries in the table
	local count = 0
	for k, v in pairs(t) do
		assert(type(k) == "userdata")
		assert(type(v) == "userdata")
		count = count + 1
	end
	assert(count == 2, "got " .. count)
	-- and c1-as-key should be identical to c1-as-value (same for c2)
	-- of course I can't be sure that enumerating the table will start with key c1, but that's not important
	local c1_as_key, c2_as_value = next(t)
	assert(type(c1_as_key) == "userdata" and type(c2_as_value) == "userdata" and c1_as_key:get() ~= c2_as_value:get())
	local c2_as_key, c1_as_value = next(t, c1_as_key)
	assert(type(c2_as_key) == "userdata" and type(c1_as_value) == "userdata" and c2_as_key:get() ~= c1_as_value:get())
	assert(c1_as_key == c1_as_value)
	assert(c2_as_key == c2_as_value)
end
