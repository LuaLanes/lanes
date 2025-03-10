local which_tests, remaining_tests = {}, {}
for k,v in ipairs{...} do
	print("got arg:", type(v), tostring(v))
	which_tests[v] = true
	remaining_tests[v] = true
end

-- ##################################################################################################

local lanes = require "lanes" .configure{ with_timers = false}

local SLEEP = function(...)
	-- just for fun: start a lane that will do the sleeping for us
	local sleeperBody = function(...)
		local lanes = require "lanes"
		local k, v = lanes.sleep(...)
		assert(k == nil and v == "timeout")
		return true
	end
	local sleeper = lanes.gen("*", sleeperBody)(...)
	-- then wait for the lane to terminate
	sleeper:join()
end

local linda = lanes.linda()
-- a numeric value to read
linda:set("val", 33.0)

-- so that we can easily swap between lanes.gen and lanes.coro, to try stuff
-- TODO: looks like the result changes when using LuaJIT and coro together. to be investigated
local generator = lanes.gen

-- ##################################################################################################

if not next(which_tests) or which_tests.genlock then
	remaining_tests.genlock = nil
	print "\n\n####################################################################\nbegin genlock & genatomic cancel test\n"

	-- get a lock and a atomic operator
	local lock = lanes.genlock(linda, "lock", 1)
	local atomic = lanes.genatomic(linda, "atomic")

	local check_returned_cancel_error = function(_status, _err)
		assert(_status == nil and _err == lanes.cancel_error)
	end
	-- check that cancelled lindas give cancel_error as they should
	linda:cancel()
	check_returned_cancel_error(linda:set("empty", 42))
	check_returned_cancel_error(linda:get("empty"))
	check_returned_cancel_error(linda:send("empty", 42))
	check_returned_cancel_error(linda:receive("empty"))
	check_returned_cancel_error(linda:limit("empty", 5))
	check_returned_cancel_error(linda:restrict("empty", "set/get"))
	assert(lanes.genlock(linda, "any", 1) == lanes.cancel_error)
	assert(lanes.genatomic(linda, "any") == lanes.cancel_error)

	-- check that lock and atomic functions return cancel_error if the linda was cancelled
	assert(lock(1) == lanes.cancel_error)
	assert(lock(-1) == lanes.cancel_error)
	assert(atomic(1) == lanes.cancel_error)

	-- reset the linda so that the other tests work
	linda:cancel("none")
	linda:limit("lock", "unlimited")
	linda:set("lock")
	linda:limit("atomic", "unlimited")
	linda:set("atomic")

	print "test OK"
end

-- ##################################################################################################

local waitCancellation = function(h, expected_status)
	local l = lanes.linda()
	if expected_status ~= "running" then
		repeat
			-- print("lane status:", h.status)
			SLEEP(0.1) -- wait a bit
		until h.status ~= "running"
	end
	print("lane status:", h.status)
	assert(h.status == expected_status, "lane status " .. h.status .. " (actual) ~= " .. expected_status .. " (expected)")
	print "test OK"
end

local laneBody = function(mode_, payload_)
	local name = "laneBody("..tostring(mode_)..","..tostring(payload_)..")"
	lane_threadname(name)

	set_finalizer(function(err, stk)
		if err == lanes.cancel_error then
			-- note that we don't get the cancel_error when running wrapped inside a protected call if it doesn't rethrow it
			print("			laneBody after cancel" )
		elseif err then
			print("			laneBody error: "..tostring(err))
		else
			print("			laneBody finalized")
		end
	end)

	print("			entering " , name)
	repeat
		if mode_ == "receive" then
			-- linda mode
			io.stdout:write("			lane calling receive() ... ")
			local key, val = linda:receive(payload_, "boob")
			print(tostring(key), val == lanes.cancel_error and "cancel_error" or tostring(val))
			if val == lanes.cancel_error then
				break -- gracefully abort loop
			end
		elseif mode_ == "get" then
			-- busy wait mode getting data from the linda
			io.stdout:write("			lane busy waiting ... ")
			for i = 1, payload_ do
				-- force a non-jitable call
				local _, a = linda:get("val")
				a = a * 2
			end
			print("again?")
		elseif mode_ == "busy" then
			-- busy wait mode in pure Lua code
			io.stdout:write("			lane busy waiting ... ")
			local _, a = linda:get("val")
			for i = 1, payload_ do
				a = a * 2
				a = math.sin(a) * math.sin(a) + math.cos(a) * math.cos(a) -- aka 1
			end
			print("again?")
		else
			error "no mode: raise an error"
		end
	until cancel_test() -- soft cancel self test
	print "			lane shutting down after breaking out of loop"
end

local protectedBody = function(...)
	local ce = lanes.cancel_error
	local errorHandler = function(_msg)
		-- forward the message to the main thread that will display it with a popup
		print("			error handler got ", ce == _msg and "cancel_error" or tostring(_msg))
		return _msg
	end
	-- Lua 5.1 doesn't pass additional xpcall arguments to the called function
	-- therefore we need to create a closure that has no arguments but pulls everything from its upvalue
	local params = {...}
	local unpack = table.unpack or unpack -- unpack for 5.1, table.unpack for 5.2+
	local paramLessClosure = function() laneBody(unpack(params)) end
	local status, message = xpcall(paramLessClosure, errorHandler)
	if status == false then
		print("			error handler rethrowing '" .. (ce == message and "cancel_error"or tostring(message)) .. "'")
		-- if the error isn't rethrown, the lane's finalizer won't get it
		error(message)
	end
end

-- ##################################################################################################
-- ##################################################################################################

if not next(which_tests) or which_tests.linda then
	remaining_tests.linda = nil
	print "\n\n####################################################################\nbegin linda cancel test\n"
	h = generator("*", laneBody)("receive", nil) -- start an infinite wait on the linda

	print "wait 1s"
	SLEEP(1)

	-- linda cancel: linda:receive() returns nil,cancel_error immediately
	print "cancelling - both"
	linda:cancel("both")

	-- wait until cancellation is effective.
	waitCancellation(h, "done")

	-- reset the linda so that the other tests work
	linda:cancel("none")
end

-- ##################################################################################################

if not next(which_tests) or which_tests.soft then
	remaining_tests.soft = nil
	print "\n\n####################################################################\nbegin soft cancel test\n"
	h = generator("*", protectedBody)("receive") -- start an infinite wait on the linda

	print "wait 1s"
	SLEEP(1)

	-- soft cancel, no awakening of waiting linda operations, should timeout
	local a, b = h:cancel("soft", 1, false)
	-- cancellation should fail as the lane is still waiting on its linda
	assert(a == false and b == "timeout")
	waitCancellation(h, "waiting")

	-- soft cancel, this time awakens waiting linda operations, which returns cancel_error immediately, no timeout.
	print "cancelling - soft"
	h:cancel("soft", true)

	-- wait until cancellation is effective. the lane will interrupt its loop and print the exit message
	waitCancellation(h, "done")
end

-- ##################################################################################################

if not next(which_tests) or which_tests.hook then
	remaining_tests.hook = nil
	print "\n\n####################################################################\nbegin hook cancel test\n"
	h = generator("*", protectedBody)("get", 300000)
	print "wait 2s"
	SLEEP(2)

	-- count hook cancel after some instruction instructions
	print "cancelling - line"
	h:cancel("line", 300, 5.0)

	-- wait until cancellation is effective. the lane will interrupt its loop and print the exit message
	waitCancellation(h, "cancelled")
end

-- ##################################################################################################

if not next(which_tests) or which_tests.hard then
	remaining_tests.hard = nil
	print "\n\n####################################################################\nbegin hard cancel test\n"
	h = lanes.gen("*", protectedBody)("receive", nil) -- infinite timeout

	-- wait 2s before cancelling the lane
	print "wait 2s"
	SLEEP(2)

	-- hard cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
	print "cancelling - hard"
	h:cancel()

	-- wait until cancellation is effective. the lane will be stopped by the linda operation throwing an error
	waitCancellation(h, "cancelled")
end

-- ##################################################################################################

if not next(which_tests) or which_tests.hard_unprotected then
	remaining_tests.hard_unprotected = nil
	print "\n\n####################################################################\nbegin hard cancel test with unprotected lane body\n"
	h = generator("*", laneBody)("receive", nil)

	-- wait 2s before cancelling the lane
	print "wait 2s"
	SLEEP(2)

	-- hard cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
	print "cancelling - hard"
	h:cancel()

	-- wait until cancellation is effective. the lane will be stopped by the linda operation throwing an error
	waitCancellation(h, "cancelled")
end

-- ##################################################################################################

local unknown_test, val = next(remaining_tests)
assert(not unknown_test, tostring(unknown_test) .. " test is unknown")

print "\nTHE END"

