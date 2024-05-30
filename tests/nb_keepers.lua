-- 2 keepers in addition to the one reserved for the timer linda
local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure{nb_user_keepers = 2}
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "assert"    -- assert.fails()
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local createLinda = function(...)
	return lanes.linda(...)
end

-- should succeed
assert.failsnot(function() createLinda("one", 1) end)
assert.failsnot(function() createLinda("two", 2) end)
-- should fail
assert.fails(function() createLinda("none") end)
assert.fails(function() createLinda("zero", 0) end)
assert.fails(function() createLinda("three", 3) end)
print "TEST OK"
