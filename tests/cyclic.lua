--
-- CYCLIC.LUA
--
-- Test program for Lua Lanes
--

local lanes = require "lanes"

local table_concat= assert(table.concat)

local function WR(str,...)
    for i=1,select('#',...) do
        str= str.."\t"..tostring( select(i,...) )
    end
    io.stderr:write( str..'\n' )
end

local function same(k,l)
    return k==l and "same" or ("NOT SAME: "..k.." "..l)
end

local a= {}
local b= {a}
a[1]= b

-- Getting the tables as upvalues should still have the <-> linkage
--
local function lane1()
    WR( "Via upvalue: ", same(a,b[1]), same(a[1],b) )
    assert( a[1]==b )
    assert( b[1]==a )
    return true
end
local L1= lanes.gen( "io", { name = 'auto' }, lane1 )()
    -- ...running

-- Getting the tables as arguments should also keep the linkage
--
local function lane2( aa, bb )
    WR( "Via arguments:", same(aa,bb[1]), same(aa[1],bb) )
    assert( aa[1]==bb )
    assert( bb[1]==aa )
    return true
end
local L2= lanes.gen( "io", { name = 'auto' }, lane2 )( a, b )
    -- ...running

-- Really unnecessary, but let's try a directly recursive table
--
c= {}
c.a= c

local function lane3( cc )
    WR( "Directly recursive: ", same(cc, cc.a) )
    assert( cc and cc.a==cc )
    return true
end
local L3= lanes.gen("io", { name = 'auto' }, lane3)(c)

-- Without a wait, exit from the main lane will close the process
--
-- Waiting for multiple lanes at once could be done using a Linda
-- (but we're okay waiting them in order)
--
L1:join()
L2:join()
L3:join()
