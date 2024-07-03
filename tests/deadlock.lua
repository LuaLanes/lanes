-- this script tests the fix of a bug that could cause the mutex of a keeper state to remain locked
-- see https://github.com/LuaLanes/lanes/commit/0cc1c9c9dcea5955f7dab921d9a2fff78c4e1729

local lanes = require "lanes"

-- Lua 5.1 compatibility
local table_unpack = table.unpack or unpack

local SLEEP = function(...)
    local k, v = lanes.sleep(...)
    assert(k == nil and v == "timeout")
end

print "let's begin"

local do_extra_stuff = true

if do_extra_stuff then
    local linda = lanes.linda "deadlock_linda"
    -- just something to make send() succeed and receive() fail
    local payload = { io.flush }

    -- lane generator. don't initialize "io" base library so that it is not known in the lane
    local g = lanes.gen('base,table', function()
        lane_threadname( "deadlock_lane")
        -- wrapping inside pcall makes the Lanes module unaware that something went wrong
        print( "In lane 1:", table_unpack{ pcall( linda.receive, linda, 'tmp')})
        -- with the bug not fixed, and non-recursive mutexes, we can hang here
        print( "In lane 2:", table_unpack{ pcall( linda.receive, linda, 'tmp')})
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
SLEEP(2)
print('we should reach here')