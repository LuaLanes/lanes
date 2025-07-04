local fixture = require "fixture"
local lanes = require "lanes".configure{on_state_create = fixture.on_state_create}

local fixture = require "fixture"
lanes.finally(fixture.throwing_finalizer)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

--------------------------------------------------
-- TEST: cancelling a suspended Lane should end it
--------------------------------------------------
if true then
	-- the generator
	local coro_g = lanes.coro("*", utils.yield_one_by_one)

	-- start the lane
	local h = coro_g("hello", "world", "!")
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
