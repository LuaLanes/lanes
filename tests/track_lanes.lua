local lanes = require "lanes" .configure{ with_timers = false, track_lanes = true}
local wait = lanes.sleep

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
    assert(count == expected_count_, "unexpected active lane count")
    print( "\n")
    return threads
end

local sleeper = function( name_, seconds_)
    -- print( "entering '" .. name_ .. "'")
    local lanes = require "lanes"
    -- no set_debug_threadname in main thread
    if set_debug_threadname
    then
        -- print( "set_debug_threadname('" .. name_ .. "')")
        set_debug_threadname( name_)
    end
    -- suspend the lane for the specified duration with a failed linda read
    lanes.sleep(seconds_)
    -- print( "exiting '" .. name_ .. "'")
end

-- sleeper( "main", 1)

-- the generator
local g = lanes.gen( "*", sleeper)

-- start a forever-waiting lane (nil timeout)
g( "forever", 'indefinitely')

-- start a lane that will last 2 seconds
g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
wait( 0.1)

-- list the known lanes (should be living lanes)
local threads = track( "============= START", 2)
--     two_seconds                        forever
assert(threads[1].status == 'waiting' and threads[2].status == 'waiting')

-- wait until "two_seconds has completed"
wait(2.1)

local threads = track( "============= two_seconds dead", 2)
--     two_seconds                        forever
assert(threads[1].status == 'done' and threads[2].status == 'waiting')

-- start another lane that will last 2 seconds, with the same name
g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
wait( 0.1)

-- list the known lanes
-- unless garbage collector cleaned it, we should have 3 lanes
local threads = track( "============= ANOTHER", 3)
--     two_seconds #2                     two_seconds #1                  forever
assert(threads[1].status == 'waiting' and threads[2].status == 'done' and threads[3].status == 'waiting')

-- this will collect the completed lane (and remove it from the tracking queue)
collectgarbage()

-- list the known lanes
local threads = track( "============= AFTER COLLECTGARBAGE", 2)
--     two_seconds #2                     forever
assert(threads[1].status == 'waiting' and threads[2].status == 'waiting')

print "done"