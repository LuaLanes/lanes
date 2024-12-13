local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "_assert"
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

local lanes_gen = assert(lanes.gen)
local lanes_linda = assert(lanes.linda)

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

PRINT("---=== Tasking (error) ===---", "\n\n")

-- a lane that throws immediately the error value it received
local g = lanes_gen("", {gc_cb = gc_cb}, error)
local errmsg = "ERROR!"

if true then
    -- if you index an errored lane, it should throw the error again
    local lane = g(errmsg)
    assert.fails(function() return lane[1] end)
    assert(lane.status == "error")
    -- even after indexing, joining a lane in error should give nil,<error>
    local a,b = lane:join()
    assert(a == nil and string.find(b, errmsg))
end

if true then
    local lane = g(errmsg)
    -- after indexing, joining a lane in error should give nil,<error>
    local a, b = lane:join()
    assert(lane.status == "error")
    assert(a == nil and string.find(b, errmsg))
    -- even after joining, indexing should raise an error
    assert.fails(function() return lane[1] end)
    -- unless we index with a negative value to get the error message
    local c = lane[-1]
    assert(c == b)
end
