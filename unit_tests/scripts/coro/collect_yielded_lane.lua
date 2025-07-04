local fixture = require "fixture"
local lanes = require "lanes".configure{on_state_create = fixture.on_state_create}

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

-- this test is only for Lua 5.4+
local utils = lanes.require "_utils54"
local PRINT = utils.MAKE_PRINT()

local out_linda = lanes.linda()

------------------------------------------------------------------------------
-- TEST: to-be-closed variables are properly closed when the lane is collected
------------------------------------------------------------------------------
if true then
	-- the generator
	local coro_g = lanes.coro("*", utils.yielder_with_to_be_closed)

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
	local coro_g = lanes.coro("*", utils.yielder_with_to_be_closed)

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
