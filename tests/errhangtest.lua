local lanes = require "lanes".configure{with_timers=false}
local linda = lanes.linda()

-- we are not allowed to send coroutines through a lane
-- however, this should raise an error, not hang the program...
if true then
	print "#### coro set"
	local coro = coroutine.create(function() end)
	print(pcall(linda.set, linda, 'test', coro))
	assert(linda:get("test") == nil)
	print "OK"
end

if true then
	print "\n#### reserved sentinels"
	print(pcall(linda.set, linda, lanes.cancel_error))
	print(pcall(linda.set, linda, linda.batched))
	assert(linda:get("test") == nil)
	print "OK"
end

-- get/set a few values
if true then
	print "\n#### set 3 -> receive batched"
	local fun = function() print "function test ok" end
	print(pcall(linda.set, linda, 'test', true, nil, fun))
	local k,b,n,f = linda:receive(linda.batched, 'test', 3) -- read back the contents
	assert(linda:get("test") == nil)
	print(k, b, n)
	f()
	print "OK"
end

-- send a table that contains a function
if true then
	print "\n#### send table with a function"
	local fun = function() print "function test ok" end
	local t_in = { [fun] = fun, fun = fun }
	print(pcall(linda.send, linda, 'test', t_in))
	local k,t_out = linda:receive('test') -- read the contents successfully sent
	t_out.fun()
	-- TODO: t_out should contain a single entry, as [fun] = fun should have been discarded because functions are not acceptable keys
	print "OK"
end

-- send a string
if true then
	print "\n#### send string"
	print(pcall(linda.send, linda, 'test', "string test ok"))
	local k,str = linda:receive('test') -- read the contents successfully sent
	print(str)
	print "OK"
end

-- we are not allowed to send coroutines through a lane
-- however, this should raise an error, not hang the program...
if true then
	print "\n#### coro send"
	local coro = coroutine.create(function() end)
	print(pcall(linda.send, linda, 'test', coro))
	assert(linda:get("test") == nil)
	print "OK"
end

-- done
print "\nSUCCESS"