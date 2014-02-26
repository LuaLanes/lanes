--
-- KEEPER.LUA
--
-- Test program for Lua Lanes
--

local lanes = require "lanes".configure{ with_timers = false, nb_keepers = 200}

local function keeper(linda)
    local mt= {
        __index= function( _, key )
            return linda:get( key )
        end,
        __newindex= function( _, key, val ) 
            linda:set( key, val )
        end
    }
    return setmetatable( {}, mt )
end

--
local lindaA= lanes.linda( "A", 1)
local A= keeper( lindaA )

local lindaB= lanes.linda( "B", 2)
local B= keeper( lindaB )

A.some= 1
print( A.some )
assert( A.some==1 )

B.some= "hoo"
assert( B.some=="hoo" )
assert( A.some==1 )

function lane()
    local a= keeper(lindaA)
    print( a.some )
    assert( a.some==1 )
    a.some= 2
end

local h= lanes.gen( "io", lane )()
h:join()

print( A.some )     -- 2
assert( A.some==2 )
