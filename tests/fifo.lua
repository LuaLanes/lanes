--
-- FIFO.LUA
--
-- Sample program for Lua Lanes
--

local lanes = require "lanes".configure{shutdown_timeout=3,with_timers=true}

local atomic_linda = lanes.linda( "atom")
local atomic_inc= lanes.genatomic( atomic_linda, "FIFO_n")

local fifo_linda = lanes.linda( "fifo")

assert( atomic_inc()==1)
assert( atomic_inc()==2)

local function FIFO()
    local my_channel= "FIFO_"..atomic_inc()

    return {
        -- Giving explicit 'nil' timeout allows numbers to be used as 'my_channel'
        --
        send = function(self, ...)
            fifo_linda:send( nil, my_channel, ...)
        end,
        receive = function(self, timeout)
            return fifo_linda:receive( timeout, my_channel)
        end,
        channel = my_channel
    }
end

local A = FIFO()
local B = FIFO()

print "Sending to A.."
A:send( 1,2,3,4,5)

print "Sending to B.."
B:send( 'a','b','c')

print "Dumping linda stats.. [1]" -- count everything
for key,count in pairs(fifo_linda:count()) do
    print("channel " .. key .. " contains " .. count .. " entries.")
    -- print(i, key_count[1], key_count[2])
end
print "Dumping linda stats.. [2]" -- query count for known channels one at a time
print("channel " .. A.channel .. " contains " .. fifo_linda:count(A.channel) .. " entries.")
print("channel " .. B.channel .. " contains " .. fifo_linda:count(B.channel) .. " entries.")
print "Dumping linda stats.. [3]" -- query counts for a predefined list of keys
for key,count in pairs(fifo_linda:count(A.channel, B.channel)) do
    print("channel " .. key .. " contains " .. count .. " entries.")
    -- print(i, key_count[1], key_count[2])
end
print "Dumping linda stats.. [4]" -- count everything
for key,contents in pairs(fifo_linda:dump()) do
    print("channel " .. key .. ": limit=".. contents.limit, " first=" .. contents.first, " count=" .. contents.count)
    for k,v in pairs(contents.fifo) do
        print("[".. k.."] = " .. v)
    end
end

print "Reading A.."
print( A:receive( 1.0))
print( A:receive( 1.0))
print( A:receive( 1.0))
print( A:receive( 1.0))
print( A:receive( 1.0))

print "Reading B.."
print( B:receive( 2.0))
print( B:receive( 2.0))
print( B:receive( 2.0))

-- Note: A and B can be passed between threads, or used as upvalues
--       by multiple threads (other parts will be copied but the 'linda'
--       handle is shared userdata and will thus point to the single place)
lanes.timer_lane:cancel() -- hard cancel, 0 timeout
lanes.timer_lane:join()