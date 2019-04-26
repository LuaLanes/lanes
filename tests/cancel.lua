local lanes = require "lanes" .configure{ with_timers = false}

local linda = lanes.linda()

--####################################################################
print "\n\n####################################################################\nbegin genlock & genatomic cancel test\n"

-- get a lock and a atomic operator
local lock = lanes.genlock( linda, "lock", 1)
local atomic = lanes.genatomic( linda, "atomic")

-- check that cancelled lindas give cancel_error as they should
linda:cancel()
assert( linda:get( "empty") == lanes.cancel_error)
assert( lanes.genlock( linda, "any", 1) == lanes.cancel_error)
assert( lanes.genatomic( linda, "any") == lanes.cancel_error)

-- check that lock and atomic functions return cancel_error if the linda was cancelled
assert( lock( 1) == lanes.cancel_error)
assert( lock( -1) == lanes.cancel_error)
assert( atomic( 1) == lanes.cancel_error)

-- reset the linda so that the other tests work
linda:cancel( "none")
linda:limit( "lock", -1)
linda:set( "lock")
linda:limit( "atomic", -1)
linda:set( "atomic")

-- a numeric value to read
linda:set( "val", 33.0)

print "test OK"
--####################################################################

local waitCancellation = function( h, expected_status)
	local l = lanes.linda()
	if expected_status ~= "running" then
		repeat
			-- print( "lane status:", h.status)
			l:receive( 0.1, "yeah") -- wait a bit
		until h.status ~= "running"
	end
	print( "lane status:", h.status)
	assert( h.status == expected_status, h.status .. " ~= " .. expected_status)
	print "test OK"
end

local laneBody = function( mode_, payload_)
	local name = "laneBody("..tostring(mode_)..","..tostring(payload_)..")"
	set_debug_threadname( name)

	set_finalizer( function( err, stk)
		if err == lanes.cancel_error then
			-- note that we don't get the cancel_error when running wrapped inside a protected call if it doesn't rethrow it
			print( "			laneBody after cancel" )
		elseif err then
			print( "			laneBody error: "..tostring(err))
		else
			print("			laneBody finalized")
		end
	end)

	print( "			entering " , name)
	repeat
		if mode_ == "receive" then
			-- linda mode
			io.stdout:write( "			lane calling receive() ... ")
			local key, val = linda:receive( payload_, "boob")
			print( lanes.cancel_error == key and "cancel_error" or tostring( key), tostring( val))
			if key == lanes.cancel_error then
				break -- gracefully abort loop
			end
		elseif mode_ == "get" then
			-- busy wait mode getting data from the linda
			io.stdout:write( "			lane busy waiting ... ")
			for i = 1, payload_ do
				-- force a non-jitable call
				local a = linda:get( "val")
				a = a * 2
			end
			print( "again?")
		elseif mode_ == "busy" then
			-- busy wait mode in pure Lua code
			io.stdout:write( "			lane busy waiting ... ")
			local a =  linda:get( "val")
			for i = 1, payload_ do
				a = a * 2
				a = math.sin( a) * math.sin( a) + math.cos( a) * math.cos( a) -- aka 1
			end
			print( "again?")
		else
			error "no mode: raise an error"
		end
	until cancel_test() -- soft cancel self test
	print "			lane shutting down after breaking out of loop"
end

local protectedBody = function( ...)
	local ce = lanes.cancel_error
	local errorHandler = function( _msg)
		-- forward the message to the main thread that will display it with a popup
		print( "			error handler got ", ce == _msg and "cancel_error"or tostring( _msg))
		return _msg
	end
	-- Lua 5.1 doesn't pass additional xpcall arguments to the called function
	-- therefore we need to create a closure that has no arguments but pulls everything from its upvalue
	local params = {...}
	local unpack = table.unpack or unpack -- unpack for 5.1, table.unpack for 5.2+
	local paramLessClosure = function() laneBody(unpack( params)) end
	local status, message = xpcall( paramLessClosure, errorHandler)
	if status == false then
		print( "			error handler rethrowing '" .. (ce == message and "cancel_error"or tostring( message)) .. "'")
		-- if the error isn't rethrown, the lane's finalizer won't get it
		error( message)
	end
end

--####################################################################
--####################################################################

print "\n\n####################################################################\nbegin linda cancel test\n"
h = lanes.gen( "*", laneBody)( "receive", nil) -- start an infinite wait on the linda

print "wait 1s"
linda:receive( 1, "yeah")

-- linda cancel: linda:receive() returns cancel_error immediately
linda:cancel( "both")

-- wait until cancellation is effective.
waitCancellation( h, "done")

-- reset the linda so that the other tests work
linda:cancel( "none")

print "\n\n####################################################################\nbegin soft cancel test\n"
h = lanes.gen( "*", protectedBody)( "receive") -- start an infinite wait on the linda

print "wait 1s"
linda:receive( 1, "yeah")

-- soft cancel, no awakening of waiting linda operations, should timeout
local a, b = h:cancel( "soft", 1, false)
-- cancellation should fail as the lane is still waiting on its linda
assert( a == false and b == "timeout")
waitCancellation( h, "waiting")

-- soft cancel, this time awakens waiting linda operations, which returns cancel_error immediately, no timeout.
h:cancel( "soft", true)

-- wait until cancellation is effective. the lane will interrupt its loop and print the exit message
waitCancellation( h, "done")

-- do return end

print "\n\n####################################################################\nbegin hook cancel test\n"
h = lanes.gen( "*", protectedBody)( "get", 300000)
print "wait 2s"
linda:receive( 2, "yeah")

-- count hook cancel after 3 instructions
h:cancel( "count", 300, 5.0)

-- wait until cancellation is effective. the lane will interrupt its loop and print the exit message
waitCancellation( h, "cancelled")

print "\n\n####################################################################\nbegin hard cancel test\n"
h = lanes.gen( "*", protectedBody)( "receive", nil) -- infinite timeout

-- wait 2s before cancelling the lane
print "wait 2s"
linda:receive( 2, "yeah")

-- hard cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
h:cancel()

-- wait until cancellation is effective. the lane will be stopped by the linda operation throwing an error
waitCancellation( h, "cancelled")

print "\n\n####################################################################\nbegin hard cancel test with unprotected lane body\n"
h = lanes.gen( "*", laneBody)( "receive", nil)

-- wait 2s before cancelling the lane
print "wait 2s"
linda:receive( 2, "yeah")

-- hard cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
h:cancel()

-- wait until cancellation is effective. the lane will be stopped by the linda operation throwing an error
waitCancellation( h, "cancelled")

print "\n\n####################################################################\nbegin kill cancel test\n"
h = lanes.gen( "*", laneBody)( "busy", 50000000) -- start a pure Lua busy loop lane

-- wait 1/3s before cancelling the lane, before the busy loop can finish
print "wait 0.3s"
linda:receive( 0.3, "yeah")

-- hard cancel with kill: the lane thread will be forcefully terminated
h:cancel( true)

-- wait until cancellation is effective. the lane will be stopped by the linda operation throwing an error
waitCancellation( h, "killed")

--####################################################################

print "\ndone"

