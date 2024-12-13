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

local function task(a, b, c)
    local new_name = "task("..a..","..b..","..c..")"
    -- test lane naming change
    lane_threadname(new_name)
    assert(lane_threadname() == new_name)
    --error "111"     -- testing error messages
    assert(hey)
    local v=0
    for i=a,b,c do
        v= v+i
    end
    return v, hey
end

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Tasking (basic) ===---", "\n\n")

local task_launch = lanes_gen("", { globals={hey=true}, gc_cb = gc_cb}, task)
    -- base stdlibs, normal priority

-- 'task_launch' is a factory of multithreaded tasks, we can launch several:

local lane1 = task_launch(100,200,3)
assert.fails(function() print(lane1[lane1]) end) -- indexing the lane with anything other than a string or a number should fail
lanes.sleep(0.1) -- give some time so that the lane can set its name
assert(lane1:get_threadname() == "task(100,200,3)", "Lane name is " .. lane1:get_threadname())

local lane2= task_launch(200,300,4)

-- At this stage, states may be "pending", "running" or "done"

local st1,st2= lane1.status, lane2.status
PRINT(st1,st2)
assert(st1=="pending" or st1=="running" or st1=="done")
assert(st2=="pending" or st2=="running" or st2=="done")

-- Accessing results ([1..N]) pends until they are available
--
PRINT("waiting...")
local v1, v1_hey= lane1[1], lane1[2]
local v2, v2_hey= lane2[1], lane2[2]

PRINT(v1, v1_hey)
assert(v1_hey == true)

PRINT(v2, v2_hey)
assert(v2_hey == true)

assert(lane1.status == "done")
assert(lane1.status == "done")
