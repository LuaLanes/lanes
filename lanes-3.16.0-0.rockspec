--
-- Lanes rockspec
--
-- Ref:
--      <http://luarocks.org/en/Rockspec_format>
--

package = "Lanes"

version = "3.16.0-0"

source= {
	url= "git+https://github.com/LuaLanes/lanes.git",
	branch= "v3.16.0"
}

description = {
	summary= "Multithreading support for Lua",
	detailed= [[
		Lua Lanes is a portable, message passing multithreading library
		providing the possibility to run multiple Lua states in parallel.
	]],
	license= "MIT/X11",
	homepage="https://github.com/LuaLanes/lanes",
	maintainer="Benoit Germain <bnt.germain@gmail.com>"
}

-- Q: What is the difference of "windows" and "win32"? Seems there is none;
--    so should we list either one or both?
--
supported_platforms= { "win32",
					   "macosx",
					   "linux",
					   "freebsd",   -- TBD: not tested
					   "msys",      -- TBD: not supported by LuaRocks 1.0 (or is it?)
}

dependencies= {
	"lua >= 5.1", -- builds with either 5.1, 5.2, 5.3 and 5.4
}

build = {
	type = "builtin",
	platforms =
	{
		linux =
		{
			modules =
			{
				["lanes.core"] =
				{
					libraries = "pthread"
				},
			}
		}
	},
	modules =
	{
		["lanes.core"] =
		{
			sources =
			{
				"src/cancel.c",
				"src/compat.c",
				"src/deep.c",
				"src/keeper.c",
				"src/lanes.c",
				"src/linda.c",
				"src/tools.c",
				"src/state.c",
				"src/threading.c",
				"src/universe.c"
			},
			incdirs = { "src"},
		},
		lanes = "src/lanes.lua"
	}
}
