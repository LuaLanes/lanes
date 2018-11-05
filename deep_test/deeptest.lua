-- create a deep-aware full userdata while Lanes isn't loaded
local dt = require "deep_test"
local deep = dt.new_deep()
deep:set(666)
print( deep)

local clonable = dt.new_clonable()

-- now load Lanes and see if that userdata is transferable
--[[
local lanes = require("lanes").configure()
local l = lanes.linda "my linda"

l:set( "key", deep)
local deep_out = l:get( "key")
print( deep_out)

lanes.register()
l:set( "key", clonable)
local clonable_out = l:get( "key")
print( clonable_out)
--]]