local lanes = require('lanes').configure()
local linda = lanes.linda "deadlock_linda"

local do_extra_stuff = true

if do_extra_stuff then
	-- just something to make send() succeed and receive() fail:
	local payload = { lanes.require('socket').connect }

	-- lane generator
	local g = lanes.gen('*', function()
		set_debug_threadname "deadlock_lane"
		print("In lane 1:", table.unpack{pcall(linda.receive, linda, 'tmp')})
		print("In lane 2:", table.unpack{pcall(linda.receive, linda, 'tmp')})
		return 33, 55
	end)

	-- send payload twice
	linda:send('tmp', payload, payload)
	local h = g()
	local err, stack = h:join()
	print('we reach here and then deadlock', err, stack)
end
-- start lane

-- wait some
lanes.sleep(2)
print('we never reach here')