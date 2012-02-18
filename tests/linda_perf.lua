local lanes = require "lanes"
lanes.configure()

-- this lane eats items in the linda one by one
local eater = function( l, loop)
	local val, key = l:receive( "go")
	for i = 1, loop do
		local val, key = l:receive( "key")
		--print( val)
	end
	-- print "loop is over"
	val, key = l:receive( "done")
	-- print( val)
end

-- this lane eats items in the linda in batches
local batched = function( l, loop, batch)
	local val, key = l:receive( "go")
	for i = 1, loop/batch do
		l:receive( l.batched, "key", batch)
	end
	print "loop is over"
	val, key = l:receive( "done")
	print( val)
end

local lane_eater_gen = lanes.gen( "*", eater)
local lane_batched_gen = lanes.gen( "*", batched)

local function ziva( preloop, loop, batch)
	-- prefill the linda a bit to increase fifo stress
	local top = math.max( preloop, loop)
	local l, lane = lanes.linda()
	local t1 = os.time()
	for i = 1, preloop do
		l:send( "key", i)
	end
	print( l:count( "key"))
	if batch then
		if l.batched then
			lane = lane_batched_gen( l, top, batch)
		else
			print "no batch support in this version of Lanes"
			lane = lane_eater_gen( l, top)
		end
	else
		lane = lane_eater_gen( l, top)
	end
	-- tell the lanes they can start eating data
	l:send("go", "go")
	-- send the remainder of the elements while they are consumed
	if loop > preloop then
		for i = preloop + 1, loop do
			l:send( "key", i)
		end
	end
	l:send( "done" ,"are you happy?")
	lane:join()
	return os.difftime(os.time(), t1)
end

local tests =
{
	--[[{ 2000000, 0},
	{ 3000000, 0},
	{ 4000000, 0},
	{ 5000000, 0},
	{ 6000000, 0},]]
	--[[{ 1000000, 2000000},
	{ 2000000, 3000000},
	{ 3000000, 4000000},
	{ 4000000, 5000000},
	{ 5000000, 6000000},]]
	--[[{ 4000000, 0},
	{ 4000000, 0, 1},
	{ 4000000, 0, 2},
	{ 4000000, 0, 3},
	{ 4000000, 0, 5},
	{ 4000000, 0, 8},
	{ 4000000, 0, 13},
	{ 4000000, 0, 21},
	{ 4000000, 0, 44},]]
}
for k, v in pairs( tests) do
	local pre, loop, batch = v[1], v[2], v[3]
	print( pre, loop, batch, "duration = " .. ziva( pre, loop, batch))
end

--[[
	V 2.1.0:
	ziva( 20000, 0) -> 4s   	ziva( 10000, 20000) -> 3s
	ziva( 30000, 0) -> 8s     ziva( 20000, 30000) -> 7s
	ziva( 40000, 0) -> 15s    ziva( 30000, 40000) -> 15s
	ziva( 50000, 0) -> 24s    ziva( 40000, 50000) -> 23s
	ziva( 60000, 0) -> 34s    ziva( 50000, 60000) -> 33s

	SIMPLIFIED:
	ziva( 20000, 0) -> 4s   	ziva( 10000, 20000) -> 3s
	ziva( 30000, 0) -> 9s     ziva( 20000, 30000) -> 8s
	ziva( 40000, 0) -> 15s    ziva( 30000, 40000) -> 15s
	ziva( 50000, 0) -> 25s    ziva( 40000, 50000) -> 24s
	ziva( 60000, 0) -> 35s    ziva( 50000, 60000) -> 35s

	FIFO:
	ziva( 2000000, 0) -> 9s   ziva( 1000000, 2000000) -> 33s
	ziva( 3000000, 0) -> 14s  ziva( 2000000, 3000000) -> 40s
	ziva( 4000000, 0) -> 20s  ziva( 3000000, 4000000) -> 27s
	ziva( 5000000, 0) -> 24s  ziva( 4000000, 5000000) -> 42s
	ziva( 6000000, 0) -> 29s  ziva( 5000000, 6000000) -> 55s

	FIFO BATCHED:
	ziva( 4000000, 0, 1)  -> 20s
	ziva( 4000000, 0, 2)  -> 11s
	ziva( 4000000, 0, 3)  -> 7s
	ziva( 4000000, 0, 5)  -> 5s
	ziva( 4000000, 0, 8)  -> 3s
	ziva( 4000000, 0, 13) -> 3s
	ziva( 4000000, 0, 21) -> 3s
	ziva( 4000000, 0, 44) -> 2s
]]

local function ziva2( preloop, loop, batch)
	local l = lanes.linda()
	-- prefill the linda a bit to increase fifo stress
	local top, step = math.max( preloop, loop), (l.batched and batch) and batch or 1
	local batch_send, batch_read
	if l.batched and batch then
		local batch_values = {}
		for i = 1, batch do
			table.insert( batch_values, i)
		end
		-- create a function that can send several values in one shot
		batch_send = function()
			l:send( "key", unpack( batch_values))
		end
		batch_read = function()
			l:receive( l.batched, "key", batch)
		end
	else -- not batched
		batch_send = function()
			l:send( "key", top)
		end
		batch_read = function()
			l:receive( "key")
		end
	end
	local t1 = os.time()
	-- first, prime the linda with some data
	for i = 1, preloop, step do
		batch_send()
	end
	-- loop that alternatively sends and reads data off the linda
	if loop > preloop then
		for i = preloop + 1, loop, step do
			batch_send()
			batch_read()
		end
	end
	-- here, we have preloop elements still waiting inside the linda
	for i = 1, preloop, step do
			batch_read()
	end
	return os.difftime(os.time(), t1)
end

local tests2 =
{
	--[[{ 2000000, 0},
	{ 3000000, 0},
	{ 4000000, 0},
	{ 5000000, 0},
	{ 6000000, 0},
	{ 1000000, 2000000},
	{ 2000000, 3000000},
	{ 3000000, 4000000},
	{ 4000000, 5000000},
	{ 5000000, 6000000},]]
	{ 4000000, 0},
	{ 4000000, 0, 1},
	{ 4000000, 0, 2},
	{ 4000000, 0, 3},
	{ 4000000, 0, 5},
	{ 4000000, 0, 8},
	{ 4000000, 0, 13},
	{ 4000000, 0, 21},
	{ 4000000, 0, 44},
}
for k, v in pairs( tests2) do
	local pre, loop, batch = v[1], v[2], v[3]
	print( pre, loop, batch, "duration = " .. ziva2( pre, loop, batch))
end

--[[
	V 2.1.0:
	ziva( 20000, 0) -> 3s   	ziva( 10000, 20000) -> 3s
	ziva( 30000, 0) -> 8s     ziva( 20000, 30000) -> 7s
	ziva( 40000, 0) -> 15s    ziva( 30000, 40000) -> 14s
	ziva( 50000, 0) -> 24s    ziva( 40000, 50000) -> 22s
	ziva( 60000, 0) -> 34s    ziva( 50000, 60000) -> 33s

	SIMPLIFIED:
	ziva( 20000, 0) -> 4s   	ziva( 10000, 20000) -> 3s
	ziva( 30000, 0) -> 8s     ziva( 20000, 30000) -> 7s
	ziva( 40000, 0) -> 14s    ziva( 30000, 40000) -> 14s
	ziva( 50000, 0) -> 23s    ziva( 40000, 50000) -> 22s
	ziva( 60000, 0) -> 33s    ziva( 50000, 60000) -> 32s

	FIFO:
	ziva( 2000000, 0) -> 9s   ziva( 1000000, 2000000) -> 14s
	ziva( 3000000, 0) -> 14s  ziva( 2000000, 3000000) -> 23s
	ziva( 4000000, 0) -> 19s  ziva( 3000000, 4000000) -> 23s
	ziva( 5000000, 0) -> 24s  ziva( 4000000, 5000000) -> 32s
	ziva( 6000000, 0) -> 29s  ziva( 5000000, 6000000) -> 33s

	FIFO BATCHED:
	ziva( 4000000, 0, 1)  -> 19s
	ziva( 4000000, 0, 2)  -> 11s
	ziva( 4000000, 0, 3)  -> s
	ziva( 4000000, 0, 5)  -> s
	ziva( 4000000, 0, 8)  -> s
	ziva( 4000000, 0, 13) -> s
	ziva( 4000000, 0, 21) -> s
	ziva( 4000000, 0, 44) -> s
]]
