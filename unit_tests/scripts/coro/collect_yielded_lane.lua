local fixture = require "fixture"
local lanes = require "lanes".configure{on_state_create = fixture.on_state_create}

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

-- a lane body that yields stuff
local yielder = function(out_linda_, wait_)
	local fixture = assert(require "fixture")
	-- here is a to-be-closed variable that, when closed, sends "Closed!" in the "out" slot of the provided linda
	local t <close> = setmetatable(
		{ text = "Closed!" }, {
			__close = function(self, err)
				if wait_ then
					fixture.block_for(wait_)
				end
				out_linda_:send("out", self.text)
			end
		}
	)
	-- yield forever, but be cancel-friendly
	local n = 1
	while true do
		coroutine.yield("I yield!", n)
		if cancel_test and cancel_test() then -- cancel_test does not exist when run immediately (not in a Lane)
			return "I am cancelled"
		end
		n = n + 1
	end
end

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
	test_close("base", yielder)
end

---------------------------------------------------------------
-- TEST: try again with a function obtained through dump/undump
---------------------------------------------------------------
if true then
	-- note this means our yielder implementation can't have upvalues, as they are lost in the process
	test_close("dumped", load(string.dump(yielder)))
end

------------------------------------------------------------------------------
-- TEST: to-be-closed variables are properly closed whzen the lane is collected
------------------------------------------------------------------------------
if true then
	-- the generator
	local coro_g = lanes.coro("*", yielder)

	-- start the lane
	local h = coro_g(out_linda)

	-- join the lane. it should be done and give back the values resulting of the first yield point
	local r, v1, v2 = h:join()
	assert(r == true and v1 == "I yield!" and v2 == 1, "got " .. tostring(r) .. " " .. tostring(v1) .. " " .. tostring(v2))
	assert(h.status == "done", "got " .. h.status)

	-- force collection of the lane
	h = nil
	collectgarbage()

	-- I want the to-be-closed variable of the coroutine linda to be properly closed
	local s, r = out_linda:receive(0, "out")
	assert(s == "out" and r == "Closed!", "coro got " .. tostring(s) .. " " .. tostring(r)) -- THIS TEST FAILS
end

---------------------------------------------------------------------------------------------------
-- TEST: if a to-be-closed handler takes longer than the join timeout, everything works as expected
---------------------------------------------------------------------------------------------------
if true then
	-- the generator
	local coro_g = lanes.coro("*", yielder)

	-- start the lane. The to-be-closed handler will sleep for 1 second
	local h = coro_g(out_linda, 1)

	-- first join attempt should timeout
	local r, v = h:join(0.6)
	assert(r == nil and v == "timeout", "got " .. tostring(r) .. " " .. tostring(v))
	assert(h.status == "running", "got " .. h.status)

	-- join the lane again. it should be done and give back the values resulting of the first yield point
	local r, v1, v2 = h:join(0.6)
	assert(r == true and v1 == "I yield!" and v2 == 1, "got " .. tostring(r) .. " " .. tostring(v1) .. " " .. tostring(v2))
	assert(h.status == "done", "got " .. h.status)

	-- force collection of the lane
	h = nil
	collectgarbage()

	-- I want the to-be-closed variable of the coroutine linda to be properly closed
	local s, r = out_linda:receive(0, "out")
	assert(s == "out" and r == "Closed!", "coro got " .. tostring(s) .. " " .. tostring(r)) -- THIS TEST FAILS
end

--------------------------------------------------
-- TEST: cancelling a suspended Lane should end it
--------------------------------------------------
if true then
	-- the generator
	local coro_g = lanes.coro("*", yielder)

	-- start the lane
	local h = coro_g(out_linda)
	repeat until h.status == "suspended"

	-- first cancellation attempt: don't wake the lane
	local b, r = h:cancel("soft", 0.5)
	-- the lane is still blocked in its suspended state
	assert(b == false and r == "timeout" and h.status == "suspended", "got " .. tostring(b) .. " " .. tostring(r) .. " " .. h.status)

	-- cancel the Lane again, this time waking it. it will resume, and yielder()'s will break out of its infinite loop
	h:cancel("soft", nil, true)

	-- lane should be done, because it returned cooperatively when detecting a soft cancel
	assert(h.status == "done", "got " .. h.status)
end
