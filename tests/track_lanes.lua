local lanes = require "lanes" .configure{ with_timers = false, track_lanes = true}

local SLEEP = function(...)
    local k, v = lanes.sleep(...)
    assert(k == nil and v == "timeout")
end

print "hello"

local track = function( title_, expected_count_)
    print( title_)
    local count = 0
    local threads = lanes.threads()
    for k, v in pairs(threads)
    do
        print( k, v.name, v.status)
        count = count + 1
    end
    assert(count == expected_count_, "unexpected active lane count " .. count .. " ~= " .. expected_count_)
    print( "\n")
    return threads
end

local sleeper = function( name_, seconds_)
    -- no set_finalizer in main thread
    if set_finalizer
    then
        set_finalizer(function() print("Finalizing '" .. name_ .. "'") end)
    end
    -- print( "entering '" .. name_ .. "'")
    local lanes = require "lanes"
    -- no lane_threadname in main thread
    if lane_threadname
    then
        -- print( "lane_threadname('" .. name_ .. "')")
        lane_threadname( name_)
    end
    -- suspend the lane for the specified duration
    lanes.sleep(seconds_)
    -- print( "exiting '" .. name_ .. "'")
end

-- sleeper( "main", 1)

-- the generator
local g = lanes.gen( "*", sleeper)

-- start a forever-waiting lane (nil timeout)
local forever = g( "forever", 'indefinitely')

-- start a lane that will last 2 seconds
local ephemeral1 = g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
SLEEP(0.1)

-- list the known lanes (both should be living lanes)
local threads = track( "============= START", 2)
--     two_seconds                        forever
assert(threads[1].status == 'waiting' and threads[2].status == 'waiting')

-- wait until ephemeral1 has completed
SLEEP(2.1)

local threads = track( "============= two_seconds dead", 2)
--     two_seconds                     forever
assert(threads[1].status == 'done' and threads[2].status == 'waiting')

-- start another lane that will last 2 seconds, with the same name
local ephemeral2 = g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
SLEEP( 0.1)

-- list the known lanes
-- we should have 3 lanes
local threads = track( "============= ANOTHER", 3)
--     two_seconds #2                     two_seconds #1                  forever
assert(threads[1].status == 'waiting' and threads[2].status == 'done' and threads[3].status == 'waiting')

-- this will collect all lane handles.
-- since ephemeral1 has completed, it is no longer tracked, but the other 2 should still be
forever = nil
ephemeral1 = nil
ephemeral2 = nil
collectgarbage()

-- list the known lanes
local threads = track( "============= AFTER COLLECTGARBAGE", 2)
--     two_seconds #2                     forever
assert(threads[1].status == 'waiting' and threads[2].status == 'waiting')

-- wait a bit more for ephemeral2 to exit, so that we get to have a free-running lane terminate itself before shutdown
SLEEP(3)

print "============= AFTER WAITING"
-- this is printed after Universe shutdown terminates the only remaining lane, 'forever'
lanes.finally(function() print "TEST OK" end)
