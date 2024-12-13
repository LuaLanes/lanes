-- 3 keepers in addition to the one reserved for the timer linda
local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure{nb_user_keepers = 3, keepers_gc_threshold = 500}
local lanes = require_lanes_result_1

local a = lanes.linda("A", 1)
local b = lanes.linda("B", 2)
local c = lanes.linda("C", 3)

-- store each linda in the other 2
do
	a:set("kA", a, b, c)
	local nA, rA, rB, rC = a:get("kA", 3)
	assert(nA == 3 and rA == a and rB == b and rC == c)
end

do
	b:set("kB", a, b, c)
	local nB, rA, rB, rC = b:get("kB", 3)
	assert(nB == 3 and rA == a and rB == b and rC == c)
end

do
	c:set("kC", a, b, c)
	local nC, rA, rB, rC = c:get("kC", 3)
	assert(nC == 3 and rA == a and rB == b and rC == c)
end
