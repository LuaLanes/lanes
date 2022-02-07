-- this script tests the fix of a bug that could cause the mutex of a keeper state to remain locked
-- see https://github.com/LuaLanes/lanes/commit/0cc1c9c9dcea5955f7dab921d9a2fff78c4e1729

local lanes = require('lanes').configure()
local linda = lanes.linda "deadlock_linda"

print "let's begin"

local do_extra_stuff = true

if do_extra_stuff then
	-- just something to make send() succeed and receive() fail (any C function exposed by some module will do)
	local payload = { lanes.require('socket').connect }

	-- lane generator
	local g = lanes.gen('*', function()
		set_debug_threadname( "deadlock_lane")
		-- wrapping inside pcall makes the Lanes module unaware that something went wrong
		print( "In lane 1:", table.unpack{ pcall( linda.receive, linda, 'tmp')})
		-- with the bug not fixed, and non-recursive mutexes, we can hang here
		print( "In lane 2:", table.unpack{ pcall( linda.receive, linda, 'tmp')})
		-- return something out of the lane
		return 33, 55
	end)

	-- send payload twice. succeeds because sending stores a function identification string in the linda's keeper state
	linda:send( 'tmp', payload, payload)
	-- start the lane
	local h = g()
	-- wait for lane completion
	local err, stack = h:join()
	print( 'result of lane execution', err, stack)
end

-- With the bug not fixed, the linda keeper's mutex is still acquired,
-- and the program hangs when the internal linda used for timers attempts to acquire the same keeper (there is only one by default)
print('waiting a bit')
lanes.sleep(2)
print('we should reach here')