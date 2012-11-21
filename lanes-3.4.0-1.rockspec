--
-- Lanes rockspec
--
-- Ref:
--      <http://luarocks.org/en/Rockspec_format>
--

package = "Lanes"

version = "3.4.0-1"

source= {
	url= "git://github.com/LuaLanes/lanes.git",
	branch= "v3.4.0"
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
	"lua >= 5.1", -- builds with either 5.1 and 5.2
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
			sources = { "src/lanes.c", "src/keeper.c", "src/tools.c", "src/threading.c"},
			incdirs = { "src"},
		},
		lanes = "src/lanes.lua"
	}
}
