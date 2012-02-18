local lanes = require "lanes"
lanes.configure()

local options = {globals = { b = 666 }}

-- local gen1 = lanes.gen("*", "dofile('fibonacci.lua')")
local gen2 = lanes.gen(options, "return b")

-- fibLane = gen1()
retLane1, retLane2 = gen2(), gen2()
-- fibLane:join()

print( retLane1[1], retLane2[1])
