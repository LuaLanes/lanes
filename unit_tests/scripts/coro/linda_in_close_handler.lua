local fixture = require "fixture"
local lanes = require "lanes".configure{on_state_create = fixture.on_state_create}

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

-- this test is only for Lua 5.4+
local utils = lanes.require "_utils54"
local PRINT = utils.MAKE_PRINT()

local out_linda = lanes.linda()

local test_close = function(what_, f_)
	local c = coroutine.create(f_)
	for i = 1, 10 do
		local t, r1, r2 = coroutine.resume(c, out_linda) -- returns true + <yielded values>
		assert(t == true and r1 == "I yield!" and r2 == i, "got " .. tostring(t) .. " " .. tostring(r1) .. " " .. tostring(r2))
		local s = coroutine.status(c)
		assert(s == "suspended")
	end
	local r, s = coroutine.close(c)
	assert(r == true and s == nil)
	-- the local variable inside the yielder body should be closed
	local s, r = out_linda:receive(0, "out")
	assert(s == "out" and r == "Closed!", what_ .. " got " .. tostring(s) .. " " .. tostring(r))
end

---------------------------------------------------------
-- TEST: first, try the close mechanism outside of a lane
---------------------------------------------------------
if true then
	assert(type(utils.yielder_with_to_be_closed) == "function")
	test_close("base", utils.yielder_with_to_be_closed)
end

---------------------------------------------------------------
-- TEST: try again with a function obtained through dump/undump
---------------------------------------------------------------
if true then
	-- note this means our yielder implementation can't have upvalues, as they are lost in the process
	test_close("dumped", load(string.dump(utils.yielder_with_to_be_closed)))
end

