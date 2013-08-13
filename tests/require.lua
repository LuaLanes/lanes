--
-- REQUIRE.LUA
--
-- Test that 'require' works from sublanes
--
lanes = require "lanes"
lanes.configure{with_timers = false}

local function a_lane()
		print "IN A LANE"
    -- To require 'math' we still actually need to have it initialized for
    -- the lane.
    --
    require "math"
    assert( math and math.sqrt )
    assert( math.sqrt(4)==2 )

    assert( lanes==nil )
    local lanes = require "lanes".configure{with_timers = false}
    assert( lanes and lanes.gen )

    local h= lanes.gen( function() return 42 end ) ()
    local v= h[1]

    return v==42
end

local gen= lanes.gen( "math,package,string,table", {package={}},a_lane )

local h= gen()
local ret= h[1]
assert( ret==true )
