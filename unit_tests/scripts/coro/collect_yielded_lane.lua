local lanes = require "lanes"

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- a lane body that yields stuff
local yielder = function(out_linda_)
	-- here is a to-be-closed variable that, when closed, sends "Closed!" in the "out" slot of the provided linda
	local t <close> = setmetatable(
		{ text = "Closed!" }, {
			__close = function(self, err)
				out_linda_:send("out", self.text)
			end
		}
	)
	-- yield forever
	while true do
		coroutine.yield("I yield!")
	end
end

local out_linda = lanes.linda()

local test_close = function(what_, f_)
	local c = coroutine.create(f_)
	for i = 1, 10 do
		local t, r = coroutine.resume(c, out_linda) -- returns true + <yielded values>
		assert(t == true and r == "I yield!", "got " .. tostring(t) .. " " .. tostring(r))
		local s = coroutine.status(c)
		assert(s == "suspended")
	end
	local r, s = coroutine.close(c)
	assert(r == true and s == nil)
	-- the local variable inside the yielder body should be closed
	local s, r = out_linda:receive(0, "out")
	assert(s == "out" and r == "Closed!", what_ .. " got " .. tostring(s) .. " " .. tostring(r))
end

-- first, try the close mechanism outside of a lane
test_close("base", yielder)

-- try again with a function obtained through dump/undump
-- note this means our yielder implementation can't have upvalues, as they are lost in the process
test_close("dumped", load(string.dump(yielder)))

------------------------------------------------------------------------------
-- TEST: to-be-closed variables are properly closed when the lane is collected
------------------------------------------------------------------------------
if false then -- NOT IMPLEMENTED YET!

	-- the generator
	local coro_g = lanes.coro("*", yielder)

	-- start the lane
	local h = coro_g(out_linda)

	-- join it so that it reaches suspended state
	local r, v = h:join(0.5)
	assert(r == nil and v == "timeout", "got " .. tostring(r) .. " " .. tostring(v))
	assert(h.status == "suspended")

	-- force collection of the lane
	h = nil
	collectgarbage()

	-- I want the to-be-closed variable of the coroutine linda to be properly closed
	local s, r = out_linda:receive(0, "out")
	assert(s == "out" and r == "Closed!", "coro got " .. tostring(s) .. " " .. tostring(r))
end
