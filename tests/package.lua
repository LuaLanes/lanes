local loaders = package.loaders or package.searchers

assert(nil == loaders[5])

local configure_loaders = function(type_)
	table.insert(loaders, 4, function() end)
	assert(loaders[1])
	assert(loaders[2])
	assert(loaders[3])
	assert(loaders[4])
	assert(loaders[5])
	print(type_, "loaders configured!")
end

configure_loaders("main")

for k,v in pairs(loaders) do
	print( k, type(v))
end

lanes = require "lanes"
lanes.configure{on_state_create = configure_loaders}