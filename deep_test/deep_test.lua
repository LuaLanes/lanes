-- create a deep-aware full userdata while Lanes isn't loaded
local dt = require "deep_test"
local deep = dt.new_deep()
print( deep)

-- now load Lanes and see if that userdata is transferable

local lanes = require("lanes").configure()

local l = lanes.linda "my linda"
l.put( "key", deep)
local out = l.get( "key")
print( out)