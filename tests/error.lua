--
-- Error reporting
--
-- Note: this code is supposed to end in errors; not included in 'make test'
--

local lanes = require "lanes".configure{ with_timers = false}

local function lane( mode_)
    set_error_reporting( mode_)
    local subf= function()  -- this so that we can see the call stack
        error "aa"
        --error({})
        --error(error)
    end
    local subf2= function()
        subf()
    end
    subf2()
end

local function cleanup(err)
end

local lgen = lanes.gen("*", { --[[finalizer=cleanup]] }, lane)

---
io.stderr:write( "\n** Error catching **\n" )
--

local error_reporting_mode = "extended"
local h= lgen( error_reporting_mode)
local _,err,stack= h:join()   -- wait for the lane (no automatic error propagation)

if err then
    assert( type(stack)=="table" ) -- only true if lanes was compiled with ERROR_FULL_STACK == 1
    io.stderr:write( "Lane error: "..tostring(err).."\n" )

	if error_reporting_mode == "basic" then -- each stack line is a string in basic mode
		io.stderr:write( "\t", table.concat(stack,"\n\t"), "\n" );
	else -- each stack line is a table in extended mode
		for line, details in pairs( stack) do
			io.stderr:write( "\t", tostring( line), "\n" );
			for detail, value in pairs( details) do
				io.stderr:write( "\t\t", tostring( detail), ":", tostring( value), "\n")
			end
		end
	end
end

---
io.stderr:write( "\n** Error propagation **\n" )
--
local h2= lgen( error_reporting_mode)
local _= h2[0]
assert(false)   -- does NOT get here

--never ends
