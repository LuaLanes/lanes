--
-- Bugs filed by irayo Jul-2008
--
--[[
"Another issue I've noticed is trying to pass a table with a function
that uses closures in it as a global variable into a new lane.  This
causes a segmentation fault and it appears to be related to the
luaG_inter_move function near line 835-836 or so in lanes.c, but I
haven't investigated further.
e.g. { globals = { data = 1, func = function() useclosurehere() end } }"
]]

local lanes = require "lanes"
lanes.configure()

local function testrun()
    assert( print )
    assert( data==1 )
    assert( type(func)=="function" )
    func()  -- "using the closure"
    return true
end

-- When some function dereferences a global key, the associated global in the source state
-- isn't sent over the target lane
-- therefore, the necessary functions must either be pulled as upvalues (hence locals)
-- or the globals must exist in the target lanes because the modules have been required there
--
local print=print
local assert=assert

local function useclosurehere()
    assert( print )
    print "using the closure" 
end

local lane= lanes.gen( "", { globals = { data=1, func=useclosurehere } }, testrun )()
print(lane[1])
