local lanes = require "lanes"

-- a newly created linda doesn't contain anything
local l = lanes.linda()

-- send a table with subtables, both as keys and values, making sure the structure is preserved

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
