local lanes = require "lanes" .configure{ with_timers = false, track_lanes = true}

local wait
do
    local linda = lanes.linda()
    wait = function( seconds_)
        linda:receive( seconds_, "dummy_key")
    end
end

print "hello"

local track = function( title_)
    print( title_)
    for k, v in pairs( lanes.threads())
    do
        print( k, v.name, v.status)
    end
    print( "\n")
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
    wait( seconds_)
    -- print( "exiting '" .. name_ .. "'")
end

-- sleeper( "main", 1)

-- the generator
local g = lanes.gen( "*", sleeper)

-- start a forever-waiting lane (nil timeout)
g( "forever")

-- start a lane that will last 2 seconds
g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
wait( 0.1)

-- list the known lanes
track( "============= START")

-- wait until "two_seconds has completed"
wait(2.1)

track( "============= two_seconds dead")

-- this will collect the completed lane (and remove it from the tracking queue)
-- collectgarbage()

-- track( "============= two_seconds dead (after collectgarbage)")

-- start another lane that will last 2 seconds, with the same name
g( "two_seconds", 2)
 
-- give a bit of time to reach the linda waiting call
wait( 0.1)

-- list the known lanes
track( "============= ANOTHER")

print "done"