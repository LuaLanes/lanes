local fixture = require "fixture" -- require fixture before lanes so that it is not registered and will cause transfer errors
local lanes = require("lanes").configure{verbose_errors = true}
local l = lanes.linda{name = "my linda"}


local do_test = function(key_)
	local t = { subtable = { [key_] = { ud = fixture.newuserdata() } } }
	local b, e = pcall(l.send, l, "k", t)
	assert(b == false and type(e) == "string")
	local t_key = type(key_)
	if t_key == "string" then
		-- expecting an error about arg#2.subtable.<key_>.ud
		local x, y = string.find(e, "arg#2.subtable." .. key_ .. ".ud", 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "boolean" then
		-- expecting an error about arg#2.subtable[true|false].ud
		local x, y = string.find(e, "arg#2.subtable[" .. tostring(key_) .. "].ud", 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "number" then
		-- expecting an error about arg#2.subtable[<number>].ud
		local x, y = string.find(e, "arg#2.subtable[" .. key_ .. "].ud", 1, true)
		assert(x and y, "got " .. e)
	elseif t_key == "userdata" then
		local t_name
		-- light userdata is formatted by std::format, where the pointer is written as a lowercase hex literal
		local expected = "arg#2.subtable[U:0x" .. string.lower(string.match(tostring(key_), "userdata: (%x+)")) .. "].ud"
		-- expecting an error about arg#2.subtable[U:0x<some hex value>].ud
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
