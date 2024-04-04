local lanes = require "lanes"
lanes.configure{ with_timers = false }

-- set TEST1, PREFILL1, FILL1, TEST2, PREFILL2, FILL2 from the command line

-- Lua 5.1/5.2 compatibility
local table_unpack = unpack or table.unpack

local finalizer = function(err, stk)
	if err == lanes.cancel_error then
		-- note that we don't get the cancel_error when running wrapped inside a protected call if it doesn't rethrow it
		print("			laneBody after cancel" )
	elseif err then
		print("			laneBody error: "..tostring(err))
	else
		print("			laneBody finalized")
	end
end

-- this lane eats items in the linda one by one
local eater = function( l, loop)
	set_finalizer(finalizer)
	-- wait for start signal
	l:receive( "go")
	-- eat data one by one
	for i = 1, loop do
		local key, val = l:receive( "key")
		-- print("eater:", val)
	end
	-- print "loop is over"
	key, val = l:receive( "done")
	print("eater: done ("..val..")")
end

-- this lane eats items in the linda in batches
local gobbler = function( l, loop, batch)
	set_finalizer(finalizer)
	-- wait for start signal
	l:receive( "go")
	-- eat data in batches
	for i = 1, loop/batch do
		l:receive( l.batched, "key", batch)
		-- print("gobbler:", batch)
	end
	print "loop is over"
	key, val = l:receive( "done")
	print("gobbler: done ("..val..")")
end

local lane_eater_gen = lanes.gen( "*", {priority = 3}, eater)
local lane_gobbler_gen = lanes.gen( "*", {priority = 3}, gobbler)

-- main thread writes data while a lane reads it
local function ziva( preloop, loop, batch)
	-- prefill the linda a bit to increase fifo stress
	local top = math.max( preloop, loop)
	local l, lane = lanes.linda()
	local t1 = lanes.now_secs()
	for i = 1, preloop do
		l:send( "key", i)
	end
	print( "stored " .. l:count( "key") .. " items in the linda before starting consumer lane")
	if batch > 0 then
		if l.batched then
			lane = lane_gobbler_gen( l, top, batch)
		else
			print "no batch support in this version of Lanes"
			lane = lane_eater_gen( l, top)
		end
	else
		lane = lane_eater_gen( l, top)
	end
	-- tell the consumer lane it can start eating data
	l:send( "go", true)
	-- send the remainder of the elements while they are consumed
	-- create a function that can send several values in one shot
	batch = math.max( batch, 1)
	local batch_values = {}
	for i = 1, batch do
		table.insert( batch_values, i)
	end
	local batch_send_log = "main: sending "..batch.." values"
	local batch_send = function()
		-- print(batch_send_log)
		l:send( "key", table_unpack( batch_values))
	end
	if loop > preloop then
		for i = preloop + 1, loop, batch do
			batch_send()
		end
	end
	l:send( "done" ,"are you happy?")
	lane:join()
	return lanes.now_secs() - t1
end

TEST1 = TEST1 or 1000
PREFILL1 = PREFILL1 or 10000
FILL1 = FILL1 or 2000000

local tests1 =
{
	{ PREFILL1, FILL1, 0},
	{ PREFILL1, FILL1, 1},
	{ PREFILL1, FILL1, 2},
	{ PREFILL1, FILL1, 3},
	{ PREFILL1, FILL1, 5},
	{ PREFILL1, FILL1, 8},
	{ PREFILL1, FILL1, 13},
	{ PREFILL1, FILL1, 21},
	{ PREFILL1, FILL1, 44},
}
print "############################################ tests #1"
for i, v in ipairs( tests1) do
	if i > TEST1 then break end
	local pre, loop, batch = v[1], v[2], v[3]
	print("-------------------------------------------------\n")
	print("START", "prefill="..pre, "fill="..loop, "batch="..batch)
	print("DURATION = " .. ziva( pre, loop, batch) .. "\n")
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

-- sequential write/read (no parallelization involved)
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
			l:send( "key", table_unpack( batch_values))
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
	local t1 = lanes.now_secs()
	-- first, prime the linda with some data
	for i = 1, preloop, step do
		batch_send()
	end
	print( "stored " .. (l:count( "key") or 0) .. " items in the linda before starting consumer lane")
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
	return lanes.now_secs() - t1
end

TEST2 = TEST2 or 1000
PREFILL2 = PREFILL2 or 0
FILL2 = FILL2 or 4000000

local tests2 =
{
	-- prefill, then consume everything
	--[[
	{ 4000000, 0},
	{ 4000000, 0, 1},
	{ 4000000, 0, 2},
	{ 4000000, 0, 3},
	{ 4000000, 0, 5},
	{ 4000000, 0, 8},
	{ 4000000, 0, 13},
	{ 4000000, 0, 21},
	{ 4000000, 0, 44},
	--]]
	-- alternatively fill and consume
	{ PREFILL2, FILL2},
	{ PREFILL2, FILL2, 1},
	{ PREFILL2, FILL2, 2},
	{ PREFILL2, FILL2, 3},
	{ PREFILL2, FILL2, 5},
	{ PREFILL2, FILL2, 8},
	{ PREFILL2, FILL2, 13},
	{ PREFILL2, FILL2, 21},
	{ PREFILL2, FILL2, 44},
}

print "############################################ tests #2"
for i, v in ipairs( tests2) do
	if i > TEST2 then break end
	local pre, loop, batch = v[1], v[2], v[3]
	print("-------------------------------------------------\n")
	print("START", "prefill="..pre, "fill="..loop, "batch="..(batch or "no"))
	print("DURATION = " .. ziva2( pre, loop, batch) .. "\n")
end

print "############################################"
print "THE END"