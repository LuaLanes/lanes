--
-- RECURSIVE.LUA
--
-- Test program for Lua Lanes
--

io.stderr:write("depth: ")
local function func( depth )
    io.stderr:write(depth .. " ")
    if depth <= 0 then
        return "done!"
    end

    local lanes = require "lanes"
    local lane = lanes.gen("*", func)( depth-1 )
    return lane[1]
end

local v= func(100)
assert(v=="done!")
io.stderr:write("TEST OK\n")
