
local register_func = true

local RequireAModuleThatExportsGlobalFunctions = function()
	-- grab some module that exports C functions, this is good enough for our purpose
	local lfs = require "lfs"
	-- make one of these a global
	GlobalFunc = lfs.attributes
	-- we need to register it so that it is transferable
	if register_func then
		local lanes = require "lanes"
		lanes.register( "GlobalFunc", GlobalFunc)
	end
	print "RequireAModuleThatExportsGlobalFunctions done"
end


local lanes = require "lanes".configure{ with_timers = false, on_state_create = RequireAModuleThatExportsGlobalFunctions}

-- load some module that adds C functions to the global space
RequireAModuleThatExportsGlobalFunctions()

local GlobalFuncUpval = GlobalFunc

-- a lane that makes use of the above global
local f = function()
	GlobalFuncUpval("foo")
	print "f done"
end


local g = lanes.gen( "*", f)

-- generate a lane, this will transfer f, which pulls GlobalFunc.
local h = g()