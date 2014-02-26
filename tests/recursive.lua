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
    -- lanes.configure() is available only at the first require()
    if lanes.configure then
			lanes = lanes.configure{with_timers = false}
		end
    local lane= lanes.gen("*", func)( depth+1 )
    return lane[1]
end

local v= func(0)
assert(v=="done!")
io.stderr:write("\n")
