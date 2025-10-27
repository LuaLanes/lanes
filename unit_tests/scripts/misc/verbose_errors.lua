local fixture = require "fixture" -- require fixture before lanes so that it is not registered and will cause transfer errors
local lanes = require("lanes").configure{verbose_errors = true}
local l = lanes.linda{name = "my linda"}


local do_test = function(key_)
	local t = {[key_] = fixture.newuserdata()}
	local b, e = pcall(l.send, l, "k", t)
	assert(b == false and type(e) == "string")
	local t_key = type(key_)
	if t_key == "string" then
		local x, y = string.find(e, "arg#2." .. key_, 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "boolean" then
		local x, y = string.find(e, "arg#2[" .. tostring(key_) .. "]", 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "number" then
		local x, y = string.find(e, "arg#2[" .. key_ .. "]", 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "userdata" then
		local t_name
		-- light userdata is formatted by std::format, where the pointer is written as a lowercase hex literal
		local expected = "arg#2[U:0x" .. string.lower(string.match(tostring(key_), "userdata: (%x+)")) .. "]"
		local x, y = string.find(e, expected, 1, true)
		assert(x and y, "expecting " .. expected .. " got " .. e)
	end
end

do_test("bob")
do_test(true)
do_test(false)
do_test(42)
do_test(42.44)
do_test(lanes.null)
