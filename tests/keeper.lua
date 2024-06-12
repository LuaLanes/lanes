--
-- KEEPER.LUA
--
-- Test program for Lua Lanes
--
-- TODO: there is a random crash when nb_user_keepers > 1. Will have to investigate if it rears its ugly head again
-- 3 keepers in addition to the one reserved for the timer linda
local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure{nb_user_keepers = 3, keepers_gc_threshold = 500}
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "assert"    -- assert.fails()
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

-- #################################################################################################

local print_id = 0
local P = function(whence_, ...)
    print_id = print_id + 1
    print(whence_, print_id .. ".", ...)
end

local PRINT = function(...) P("main", ...) end

local DONE = function()
    PRINT "collecting garbage"
    collectgarbage()
    PRINT "SUCCESS\n"
end

-- #################################################################################################
-- #################################################################################################

if true then
    PRINT "========================================================================================="
    PRINT "Linda groups test:"

    local createLinda = function(...)
        return lanes.linda(...)
    end

    -- should succeed
    assert.failsnot(function() createLinda("zero", 0) end)
    assert.failsnot(function() createLinda("one", 1) end)
    assert.failsnot(function() createLinda("two", 2) end)
    assert.failsnot(function() createLinda("three", 3) end)
    -- should fail (and not create the lindas)
    assert.fails(function() createLinda("minus 1", -1) end)
    assert.fails(function() createLinda("none") end)
    assert.fails(function() createLinda("four", 4) end)

end
-- should only collect the 4 successfully created lindas
DONE()

-- #################################################################################################

if true then
    PRINT "========================================================================================="
    PRINT "Linda names test:"
    local unnamedLinda1 = lanes.linda(1)
    local unnamedLinda2 = lanes.linda("", 2)
    local veeeerrrryyyylooongNamedLinda3 = lanes.linda( "veeeerrrryyyylooongNamedLinda", 3)
    assert(tostring(veeeerrrryyyylooongNamedLinda3) == "Linda: veeeerrrryyyylooongNamedLinda")
    local shortNamedLinda0 = lanes.linda( "short", 0)
    assert(tostring(shortNamedLinda0) == "Linda: short")
    PRINT(shortNamedLinda0, unnamedLinda1, unnamedLinda2, veeeerrrryyyylooongNamedLinda3)
end
-- collect the 4 lindas we created
DONE()

-- #################################################################################################

if true then
    PRINT "========================================================================================="
    PRINT "Linda GC test:"
    local a = lanes.linda("A", 1)
    local b = lanes.linda("B", 2)
    local c = lanes.linda("C", 3)

    -- store lindas in each other and in themselves
    a:set("here", lanes.linda("temporary linda", 0))
    b:set("here", a, b, c)
    c:set("here", a, b, c)

    -- repeatedly add and remove stuff in the linda 'a' so that a GC happens during the keeper operation
    for i = 1, 100 do
        io.stdout:write "."
        for j = 1, 1000 do -- send 1000 tables
            -- PRINT("send #" .. j)
            a:send("here", {"a", "table", "with", "some", "stuff"}, j)
        end
        -- PRINT(clearing)
        a:set("here") -- clear everything, including the temporary linda for which we have no reference
    end
    io.stdout:write "\n"
    b:set("here")
    c:set("here")
end
-- should successfully collect a, b, and c and destroy the underlying Deep objects
DONE()

-- #################################################################################################

if true then
    PRINT "========================================================================================="
    PRINT "General test:"

    local function keeper(linda)
        local mt= {
            __index= function(_, key)
                local _count, _val = linda:get(key)
                return _val
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

    local lindaC= lanes.linda( "C", 3)
    local C= keeper( lindaC )
    PRINT("Created", lindaA, lindaB, lindaC)

    A.some= 1
    PRINT("A.some == " .. A.some )
    assert( A.some==1 )

    B.some= "hoo"
    PRINT("B.some == " .. B.some )
    assert( B.some=="hoo" )
    assert( A.some==1 )
    assert( C.some==nil )

    function lane()
        local PRINT = function(...) P("lane", ...) end
    
        local a= keeper(lindaA)
        PRINT("a.some == " .. a.some )
        assert( a.some==1 )
        a.some= 2
        assert( a.some==2 )
        PRINT("a.some == " .. a.some )

        local c = keeper(lindaC)
        assert( c.some==nil )
        PRINT("c.some == " .. tostring(c.some))
        c.some= 3
        assert( c.some==3 )
        PRINT("c.some == " .. c.some)
        return true
    end

    PRINT("lane started")
    local h= lanes.gen( "io", lane )()
    PRINT("lane joined:", h:join())

    PRINT("A.some = " .. A.some )
    assert( A.some==2 )
    PRINT("C.some = " .. C.some )
    assert( C.some==3 )
    lindaC:set("some")
    assert( C.some==nil )
end
DONE()

print "\nTEST OK"