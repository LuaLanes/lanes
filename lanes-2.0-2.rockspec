--
-- Lanes rockspec
--
-- Ref:  
--      <http://luarocks.org/en/Rockspec_format>
--
-- History:
--  AKa 1-Sep-2008: 2.0-2 (NOT sent to list): fixed VC++ not finding DLL issue
--  AKa 20-Aug-2008: 2.0-1 sent to luarocks-developers
--

package = "Lanes"

version = "2.0-2"

source= {
    url= "http://akauppi.googlepages.com/lanes-2.0.tgz",
    md5= "27a807828de0bda3787dbcd2d4947019"
}

description = {
	summary= "Multithreading support for Lua",
	detailed= [[
        Lua Lanes is a portable, message passing multithreading library 
        providing the possibility to run multiple Lua states in parallel. 
    ]],
	license= "MIT/X11",
	homepage="http://kotisivu.dnainternet.net/askok/lanes/",
	maintainer="Asko Kauppi <akauppi@gmail.com>"	
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
    "lua >= 5.1, < 5.2",
}

--
-- Non-Win32: build using the Makefile
-- Win32: build using 'make-vc.cmd' and "manual" copy of products
--
-- TBD: How is MSYS treated?  We'd like (really) it to use the Makefile.
--      It should be a target like "cygwin", not defining "windows". 
--      "windows" should actually guarantee Visual C++ as the compiler.
--
-- Q: Does "win32" guarantee we have Visual C++ 2005/2008 command line tools?
--
-- Note: Cannot use the simple "module" build type, because we need to precompile
--       'src/keeper.lua' -> keeper.lch and bake it into lanes.c.
--
build = {

    -- Win32 (Visual C++) uses 'make-vc.cmd' for building
    --
    platforms= {
        windows= {
            type= "command",
            build_command= "make-vc.cmd",
            install= {
                lua = { "src/lanes.lua" },
                lib = { "lua51-lanes.dll" }
            }
        }
    },

    -- Other platforms use the Makefile
    --
    -- LuaRocks defines CFLAGS, LIBFLAG and LUA_INCDIR for 'make rock',
    --          defines LIBDIR, LUADIR for 'make install'
    --
    -- Ref: <http://www.luarocks.org/en/Paths_and_external_dependencies>
    --
    type = "make",
    
    build_target = "rock",
    build_variables= {
        CFLAGS= "$(CFLAGS) -I$(LUA_INCDIR)",
        LIBFLAG= "$(LIBFLAG)",
    },

    install_target = "install",
    install_variables= {
        LUA_LIBDIR= "$(LIBDIR)",
        LUA_SHAREDIR= "$(LUADIR)",
    }
}

