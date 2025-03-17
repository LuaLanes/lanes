if not package.preload.fixture then
	error "can't be run outside of UnitTest framework"
end
local fixture = require "fixture"

local lanes = require "lanes".configure{shutdown_timeout = 0.001, on_state_create = fixture.on_state_create}

-- launch lanes that blocks forever
local lane = function()
	local fixture = require "fixture"
	fixture.sleep_for()
end

-- the generator
local g1 = lanes.gen("*", { name = 'auto' }, lane)

-- launch lane
local h1 = g1()

-- wait until the lane is running
repeat until h1.status == "running"

-- this finalizer returns an error string that telling Universe::__gc will use to raise an error when it detects the uncooperative lane
lanes.finally(fixture.throwing_finalizer)
