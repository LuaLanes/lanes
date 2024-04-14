local lanes = require "lanes".configure{with_timers=false}

local linda = lanes.linda()

local coro = coroutine.create(function() end)

local fun = function() print "fun" end
local t_in = { [fun] = fun, fun = fun }

-- send a string
print( pcall(linda.send,linda, 'test', "oh boy"))
-- send a table that contains a function
print( pcall(linda.send,linda, 'test', t_in))
-- we are not allowed to send coroutines through a lanes
-- however, this should raise an error, not hang the program...
print( pcall(linda.send,linda, 'test', coro))
k,str = linda:receive('test') -- read the contents successfully sent
print( str) -- "oh boy"
k,t_out = linda:receive('test') -- read the contents successfully sent
t_out.fun() -- "fun"
-- linda:send( 'test', coro)
print "SUCCESS"