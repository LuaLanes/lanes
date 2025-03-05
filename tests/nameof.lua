local lanes = require "lanes".configure{nb_user_keepers = 100, on_state_create = function() end}

local SLEEP = function(...)
	local k, v = lanes.sleep(...)
	assert(k == nil and v == "timeout")
end

print("Name of table: ", lanes.nameof({}))
print("Name of string.sub: ", lanes.nameof(string.sub))
print("Name of print: ", lanes.nameof(print))

-- a standalone function without any dependency
local body = function()
	local n = 0
	for i = 1, 1e30 do
		n = n + 1
	end
end

-- start the lane without any library
local h = lanes.gen(nil, body)()
SLEEP(0.1)
print("Name of lane: ", lanes.nameof(h), "("..h.status..")")
assert(h.status == "running")
-- cancel the lane
h:cancel("line", 1)
SLEEP(0.1)
print("Name of lane: ", lanes.nameof(h), "("..h.status..")")
assert(h.status == "cancelled")
print "TEST OK"
