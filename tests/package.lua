assert(nil == package.loaders[5])

local configure_loaders = function()
	table.insert(package.loaders, 4, function() end)
	assert(package.loaders[1])
	assert(package.loaders[2])
	assert(package.loaders[3])
	assert(package.loaders[4])
	assert(package.loaders[5])
	print "loaders configured!"
end

configure_loaders()

for k,v in pairs(package.loaders) do
	print( k, type(v))
end

lanes = require "lanes"
lanes.configure{with_timers=false, on_state_create = configure_loaders}