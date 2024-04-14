--
-- TIMER.LUA
--
-- Sample program for Lua Lanes
--

-- On MSYS, stderr is buffered. In this test it matters.
io.stderr:setvbuf "no"


local lanes = require "lanes".configure()

local linda= lanes.linda()

local function PRINT(str)
    io.stderr:write(str.."\n")
end

local T1= "1s"  -- these keys can be anything...
local T2= "5s"
local PING_DURATION = 20

local step= {}

lanes.timer( linda, T1, 1.0, 1.0 )
step[T1]= 1.0

PRINT( "\n*** Starting 1s Timer (not synced to wall clock) ***\n" )

local v_first
local v_last= {}     -- { [channel]= num }
local T2_first_round= true

local caught= {}     -- { [T1]= bool, [T2]= bool }

while true do
    io.stderr:write("waiting...\t")
    local channel, v = linda:receive( 6.0, T1,T2 )
    assert( channel==T1 or channel==T2 )
    caught[channel]= true

    io.stderr:write( ((channel==T1) and "" or "\t\t").. string.format("%.3f",v),"\n" )
    assert( type(v)=="number" )

    if v_last[channel] then
        if channel==T2 and T2_first_round then
            -- do not make measurements, first round is not 5secs due to wall clock adjustment
            T2_first_round= false
        else
            local dt = math.abs(v-v_last[channel]- step[channel])
            assert( dt < 0.02, "channel " .. channel .. " is late: " .. dt)
        end
    end
    
    if not v_first then
        v_first= v
    elseif v-v_first > 3.0 and (not step[T2]) then
        PRINT( "\n*** Starting 5s timer (synced to wall clock) ***\n" )

        -- The first event can be in the past (just cut seconds down to 5s)
        --
        local date= os.date("*t")
        date.sec = date.sec - date.sec%5

        lanes.timer( linda, T2, date, 5.0 )
        step[T2]= 5.0

    elseif v-v_first > PING_DURATION then    -- exit condition
        break
    end
    v_last[channel]= v
end  

-- Windows version had a bug where T2 timers were not coming through, at all.
-- AKa 24-Jan-2009
--
assert( caught[T1] )
assert( caught[T2] )


PRINT( "\n*** Listing timers ***\n" )
local r = lanes.timers() -- list of {linda, key, {}}
for _,t in ipairs( r) do
    local linda, key, timer = t[1], t[2], t[3]
    print( tostring( linda), key, timer[1], timer[2])
end


PRINT( "\n*** Clearing timers ***\n" )

lanes.timer( linda, T1, 0 )    -- reset; no reoccuring ticks
lanes.timer( linda, T2, 0 )

linda:receive( 0, T1 )    -- clear out; there could be one tick left
linda:receive( 0, T2 )

assert( linda:get(T1) == nil )
assert( linda:get(T2) == nil )

PRINT "...making sure no ticks are coming..."

local k,v= linda:receive( 10, T1,T2 )    -- should not get any
assert(v==nil)

lanes.timer_lane:cancel() -- hard cancel, 0 timeout
print (lanes.timer_lane[1], lanes.timer_lane[2])