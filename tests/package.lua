local loaders = package.loaders or package.searchers

assert(nil == loaders[5])

local configure_loaders = function()
	table.insert(loaders, 4, function() end)
	assert(loaders[1])
	assert(loaders[2])
	assert(loaders[3])
	assert(loaders[4])
	assert(loaders[5])
	print "loaders configured!"
end

configure_loaders()

for k,v in pairs(loaders) do
	print( k, type(v))
end

lanes = require "lanes"
lanes.configure{with_timers=false, on_state_create = configure_loaders}