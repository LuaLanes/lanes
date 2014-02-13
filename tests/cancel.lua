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

--####################################################################

local laneBody = function( timeout_)
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

	print( "			entering lane with " .. tostring( timeout_) .. " timeout")
	repeat
		-- block-wait to be hard-cancelled
		print "			lane calling receive()"
		local key, val = linda:receive( timeout_, "boob")
		print( "			receive() -> ", lanes.cancel_error == key and "cancel_error" or tostring( key), tostring( val))
	until cancel_test() -- soft cancel self test
	print "			shutting down after breaking out of loop"
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
	local paramLessClosure = function() laneBody(table.unpack( params)) end
	local status, message = xpcall( paramLessClosure, errorHandler)
	if status == false then
		print( "			error handler rethrowing '" .. (ce == message and "cancel_error"or tostring( message)) .. "'")
		-- if the error isn't rethrown, the lane's finalizer won't get it
		error( message)
	end
end

--####################################################################

print "####################################################################\nbegin soft cancel test\n"
h = lanes.gen("*", protectedBody)( 0.666)
print "wait 3s"
linda:receive( 3, "yeah")

-- soft cancel
print "soft cancel with awakening"
h:cancel( -1, true)

-- wait 10s: the lane will interrupt its loop and print the exit message
print "wait 2s"
linda:receive( 2, "yeah")

--####################################################################

print "\n\n####################################################################\nbegin hard cancel test\n"
h = lanes.gen("*", protectedBody)()

-- wait 3s before cancelling the lane
print "wait 3s"
linda:receive( 3, "yeah")

-- hard cancel and wait 10s: the lane will be interrupted from inside its current linda:receive() and won't return from it
print "hard cancel (always awakens)"
h:cancel()

print "wait 5s"
linda:receive( 5, "yeah")

--####################################################################

print "\n\n####################################################################\nbegin hard cancel test with unprotected lane body\n"
h = lanes.gen("*", laneBody)()

-- wait 3s before cancelling the lane
print "wait 3s"
linda:receive( 3, "yeah")

-- hard cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
print "hard cancel (always awakens)"
h:cancel()

print "wait 5s"
linda:receive( 5, "yeah")

--####################################################################
print "\n\n####################################################################\nbegin linda cancel test\n"
h = lanes.gen("*", laneBody)()

-- wait 3s before cancelling the lane
print "wait 3s"
linda:receive( 3, "yeah")

-- linda cancel: the lane will be interrupted from inside its current linda:receive() and won't return from it
print "linda cancel (always awakens the lane)"
linda:cancel( "both")

print "wait 5s"
linda:receive( 5, "yeah")

--####################################################################

print "\ndone"

