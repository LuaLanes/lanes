--
-- Bugs filed by irayo Jul-2008
--
--[[
This code showed lack of caching 'select', 'type' etc. in 'src/lanes.lua'.
]]
local function recurse()
    print("level "..i);
    if i > 10 then return "finished" end

    local lanes = require "lanes"
    lanes.configure( 1, "NO_TIMERS")

    local lane = lanes.gen( "*", { globals = { ["i"]= i + 1 } }, recurse ) ()
    return lane[1]
end

i = 0;
recurse()
