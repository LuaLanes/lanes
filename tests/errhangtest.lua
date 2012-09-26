local lanes = require "lanes"
lanes.configure()

local linda = lanes.linda()

local coro = coroutine.create(function() end)

-- we are not allowed to send coroutines through a lanes
-- however, this should raise an error, not hang the program...
print( pcall(linda.send,linda, 'test', "oh boy"))
print( pcall(linda.send,linda, 'test', coro))
k,res = linda:receive('test')
print( res)
-- linda:send( 'test', coro)
