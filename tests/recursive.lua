--
-- RECURSIVE.LUA
--
-- Test program for Lua Lanes
--

io.stderr:write( "depth:" )
local function func( depth )
    io.stderr:write(" " .. depth)
    if depth > 10 then
        return "done!"
    end

    local lanes = require "lanes"
    -- lanes.configure() is gone after we call it...
    lanes.configure()
    local lane= lanes.gen("*", func)( depth+1 )
    return lane[1]
end

local v= func(0)
assert(v=="done!")
io.stderr:write("\n")
