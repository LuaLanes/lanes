local print = print

local lanes = require "lanes".configure{ with_timers = false, allocator="protected"}

local linda = lanes.linda()

local body = function( id_)
	set_finalizer( function( err, stk)
		if err == lanes.cancel_error then
			-- lane cancellation is performed by throwing a special userdata as error
			print( "after cancel")
		elseif err then
			-- no special error: true error
			print( " error: "..tostring(err))
		else
			-- no error: we just got finalized
			print( "[" .. id_ .. "] done")
		end
	end)

	print( "[" .. id_ .. "] waiting for signal")
	-- wait for the semaphore to be available
	local _, cost = linda:receive( "lock")
	-- starting to work
	print( "[" .. id_ .. "] doing some allocations")
	for i = 1, cost * (1 + id_ * 0.1) do
		local t = { "some", "table", "with", "a", "few", "fields"}
	end
	linda:send( "key", "done")
end

-- start threads
local COUNT = 4
local gen = lanes.gen( "*", body)
for i = 1, COUNT do
	gen( i)
end

-- wait a bit
print "waiting a bit ..."
linda:receive( 2, "foo")
-- tell lanes to start running
print "GO!"
for i = 1, COUNT do
	linda:send( "lock", 300000)
end

-- wait for completion
linda:receive( linda.batched, "key", COUNT)
print "SUCCESS"
