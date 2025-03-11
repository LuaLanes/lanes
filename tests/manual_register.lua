local RequireAModuleThatExportsGlobalFunctions = function(type_)
	-- grab some module that exports C functions, this is good enough for our purpose
	local due = require "deep_userdata_example"
	-- make one of these a global
	GlobalFunc = due.get_deep_count
	-- we need to register it so that it is transferable
	local lanes = require "lanes"
	lanes.register( "GlobalFunc", GlobalFunc)
	print(type_, "RequireAModuleThatExportsGlobalFunctions done:", lanes.nameof(GlobalFunc))
end


local lanes = require "lanes".configure{on_state_create = RequireAModuleThatExportsGlobalFunctions}

-- load some module that adds C functions to the global space
RequireAModuleThatExportsGlobalFunctions("main")

local GlobalFuncUpval = GlobalFunc

-- a lane that makes use of the above global
local f = function()
	GlobalFuncUpval("foo")
	print("f done:", lanes.nameof(GlobalFuncUpval))
	return 33
end


local g = lanes.gen( "*", f)

-- generate a lane, this will transfer f, which pulls GlobalFunc.
local h = g()
assert(h[1] == 33)
