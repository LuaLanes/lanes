local lanes = require "lanes"

-- Lua 5.4 specific:
if _VERSION >= "Lua 5.4" then
    -- go through a string so that the script we are in doesn't trigger a parse error with older Lua
    local res = assert(load [[
        local lanes = require 'lanes'
        local r
        do
            local h <close> = lanes.gen('*', lanes.sleep)(0.5)
            r = h
        end -- h is closed here
        return r.status
    ]])()
    -- when the do...end block is exited, the to-be-closed variable h is closed, which internally calls h:join()
    -- therefore the status of the lane should be "done"
    assert(res == "done")
    print("close result:", res)
end


local options = {globals = { b = 666 }}

local gen1 = lanes.gen("*", "return true, dofile('fibonacci.lua')")
local gen2 = lanes.gen(options, "return b")

fibLane = gen1()
lanes.sleep(0.1)
print(fibLane, fibLane.status)
local _status, _err = fibLane:join()
print(_status, _err)

retLane1, retLane2 = gen2(), gen2()

print( retLane1[1], retLane2[1])
print "TEST OK"