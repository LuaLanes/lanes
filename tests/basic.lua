--
-- BASIC.LUA           Copyright (c) 2007-08, Asko Kauppi <akauppi@gmail.com>
--
-- Selftests for Lua Lanes
--
-- To do:
--      - ...
--

local lanes = require "lanes".configure{ with_timers = false}
require "assert"    -- assert.fails()

local lanes_gen=    assert( lanes.gen )
local lanes_linda=  assert( lanes.linda )

local tostring=     assert( tostring )

local function PRINT(...)
    local str=""
    for i=1,select('#',...) do
        str= str..tostring(select(i,...)).."\t"
    end
    if io then
        io.stderr:write(str.."\n")
    end
end

local gc_cb = function( name_, status_)
	PRINT( "				---> lane '" .. name_ .. "' collected with status " .. status_)
end
--gc_cb = nil


---=== Local helpers ===---

local tables_match

-- true if 'a' is a subtable of 'b'
--
local function subtable( a, b )
    --
    assert( type(a)=="table" and type(b)=="table" )

    for k,v in pairs(b) do
        if type(v)~=type(a[k]) then
            return false    -- not subtable (different types, or missing key)
        elseif type(v)=="table" then
            if not tables_match(v,a[k]) then return false end
        else
            if a[k] ~= v then return false end
        end
    end
    return true     -- is a subtable
end

-- true when contents of 'a' and 'b' are identical
--
tables_match= function( a, b )
    return subtable( a, b ) and subtable( b, a )
end

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Tasking (basic) ===---", "\n\n")

local function task( a, b, c )
    set_debug_threadname( "task("..a..","..b..","..c..")")
    --error "111"     -- testing error messages
    assert(hey)
    local v=0
    for i=a,b,c do
        v= v+i
    end
    return v, hey
end

local task_launch= lanes_gen( "", { globals={hey=true}, gc_cb = gc_cb}, task )
	-- base stdlibs, normal priority

-- 'task_launch' is a factory of multithreaded tasks, we can launch several:

local lane1= task_launch( 100,200,3 )
local lane2= task_launch( 200,300,4 )

-- At this stage, states may be "pending", "running" or "done"

local st1,st2= lane1.status, lane2.status
PRINT(st1,st2)
assert( st1=="pending" or st1=="running" or st1=="done" )
assert( st2=="pending" or st2=="running" or st2=="done" )

-- Accessing results ([1..N]) pends until they are available
--
PRINT("waiting...")
local v1, v1_hey= lane1[1], lane1[2]
local v2, v2_hey= lane2[1], lane2[2]

PRINT( v1, v1_hey )
assert( v1_hey == true )

PRINT( v2, v2_hey )
assert( v2_hey == true )

assert( lane1.status == "done" )
assert( lane1.status == "done" )
lane1, lane2 = nil
collectgarbage()

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Tasking (cancelling) ===---", "\n\n")

local task_launch2= lanes_gen( "", { cancelstep=100, globals={hey=true}, gc_cb = gc_cb}, task )

local N=999999999
local lane9= task_launch2(1,N,1)   -- huuuuuuge...

-- Wait until state changes "pending"->"running"
--
local st
local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write( (i==1) and st.." " or '.' )
    if st~="pending" then break end
end
PRINT(" "..st)

if st=="error" then
    local _= lane9[0]  -- propagate the error here
end
if st=="done" then
    error( "Looping to "..N.." was not long enough (cannot test cancellation)" )
end
assert( st=="running" )

lane9:cancel()

local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write( (i==1) and st.." " or '.' )
    if st~="running" then break end
end
PRINT(" "..st)
assert( st == "cancelled" )

-- cancellation of lanes waiting on a linda
local limited = lanes.linda()
limited:limit( "key", 1)
-- [[################################################
limited:send( "key", "hello") -- saturate linda
local wait_send = function()
	local a,b
	set_finalizer( function() print( "wait_send", a, b) end)
	a,b = limited:send( "key", "bybye") -- infinite timeout, returns only when lane is cancelled
end

local wait_send_lane = lanes.gen( "*", wait_send)()
repeat until wait_send_lane.status == "waiting"
print "wait_send_lane is waiting"
wait_send_lane:cancel()
repeat until wait_send_lane.status == "cancelled"
print "wait_send_lane is cancelled"
--################################################]]
local wait_receive = function()
	local k, v
	set_finalizer( function() print( "wait_receive", k, v) end)
	k, v = limited:receive( "dummy") -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_lane = lanes.gen( "*", wait_receive)()
repeat until wait_receive_lane.status == "waiting"
print "wait_receive_lane is waiting"
wait_receive_lane:cancel()
repeat until wait_receive_lane.status == "cancelled"
print "wait_receive_lane is cancelled"
--################################################]]
local wait_receive_batched = function()
	local k, v1, v2
	set_finalizer( function() print( "wait_receive_batched", k, v1, v2) end)
	k, v1, v2 = limited:receive( limited.batched, "dummy", 2) -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_batched_lane = lanes.gen( "*", wait_receive_batched)()
repeat until wait_receive_batched_lane.status == "waiting"
print "wait_receive_batched_lane is waiting"
wait_receive_batched_lane:cancel()
repeat until wait_receive_batched_lane.status == "cancelled"
print "wait_receive_batched_lane is cancelled"
--################################################]]

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Communications ===---", "\n\n")

local function WR(...) io.stderr:write(...) end

local chunk= function( linda )
	set_debug_threadname "chunk"
    local function receive() return linda:receive( "->" ) end
    local function send(...) linda:send( "<-", ... ) end

    WR( "Lane starts!\n" )

    local k,v
    k,v=receive(); WR( v.." received\n" ); assert( v==1 )
    k,v=receive(); WR( v.." received\n" ); assert( v==2 )
    k,v=receive(); WR( v.." received\n" ); assert( v==3 )

    send( 1,2,3 );              WR( "1,2,3 sent\n" )
    send 'a';                   WR( "'a' sent\n" )
    send { 'a', 'b', 'c', d=10 }; WR( "{'a','b','c',d=10} sent\n" )

    k,v=receive(); WR( v.." received\n" ); assert( v==4 )

    WR( "Lane ends!\n" )
end

local linda= lanes_linda()
assert( type(linda) == "userdata" )
    --
    -- ["->"] master -> slave
    -- ["<-"] slave <- master

local function PEEK() return linda:get("<-") end
local function SEND(...) linda:send( "->", ... ) end
local function RECEIVE() local k,v = linda:receive( 1, "<-" ) return v end

local t= lanes_gen("io", {gc_cb = gc_cb}, chunk)(linda)     -- prepare & launch

SEND(1);  WR( "1 sent\n" )
SEND(2);  WR( "2 sent\n" )
for i=1,100 do
    WR "."
    assert( PEEK() == nil )    -- nothing coming in, yet
end
SEND(3);  WR( "3 sent\n" )

local a,b,c= RECEIVE(), RECEIVE(), RECEIVE()

print( "lane status: " .. t.status)
if t.status == "error" then
	print( t:join())
else
	WR( a..", "..b..", "..c.." received\n" )
end

assert( a==1 and b==2 and c==3 )

local a= RECEIVE();   WR( a.." received\n" )
assert( a=='a' )

local a= RECEIVE();   WR( type(a).." received\n" )
assert( tables_match( a, {'a','b','c',d=10} ) )

assert( PEEK() == nil )
SEND(4)

t = nil
collectgarbage()
-- wait
linda: receive( 1, "wait")

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Stdlib naming ===---", "\n\n")

local function dump_g( _x)
	set_debug_threadname "dump_g"
	assert(print)
	print( "### dumping _G for '" .. _x .. "'")
	for k, v in pairs( _G) do
		print( "\t" .. k .. ": " .. type( v))
	end
	return true
end

local function io_os_f( _x)
	set_debug_threadname "io_os_f"
	assert(print)
	print( "### checking io and os libs existence for '" .. _x .. "'")
	assert(io)
	assert(os)
	return true
end

local function coro_f( _x)
	set_debug_threadname "coro_f"
	assert(print)
	print( "### checking coroutine lib existence for '" .. _x .. "'")
	assert(coroutine)
	return true
end

assert.fails( function() lanes_gen( "xxx", {gc_cb = gc_cb}, io_os_f ) end )

local stdlib_naming_tests =
{
	-- { "", dump_g},
	-- { "coroutine", dump_g},
	-- { "io", dump_g},
	-- { "bit32", dump_g},
	{ "coroutine", coro_f},
	{ "*", io_os_f},
	{ "io,os", io_os_f},
	{ "io+os", io_os_f},
	{ "io,os,base", io_os_f},
}

for _, t in ipairs( stdlib_naming_tests) do
	local f= lanes_gen( t[1], {gc_cb = gc_cb}, t[2])     -- any delimiter will do
	assert( f(t[1])[1] )
end

collectgarbage()

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Comms criss cross ===---", "\n\n")

-- We make two identical lanes, which are using the same Linda channel.
--
local tc= lanes_gen( "io", {gc_cb = gc_cb},
  function( linda, ch_in, ch_out )
		set_debug_threadname( "criss cross " .. ch_in .. " -> " .. ch_out)
    local function STAGE(str)
        io.stderr:write( ch_in..": "..str.."\n" )
        linda:send( nil, ch_out, str )
        local k,v= linda:receive( nil, ch_in )
        assert(v==str)
    end
    STAGE("Hello")
    STAGE("I was here first!")
    STAGE("So what?")
  end
)

local linda= lanes_linda()

local a,b= tc(linda, "A","B"), tc(linda, "B","A")   -- launching two lanes, twisted comms

local _= a[1],b[1]  -- waits until they are both ready

a, b = nil
collectgarbage()

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== Receive & send of code ===---", "\n\n")

local upvalue="123"

local function chunk2( linda )
    assert( upvalue=="123" )    -- even when running as separate thread
    -- function name & line number should be there even as separate thread
    --
    local info= debug.getinfo(1)    -- 1 = us
    --
    for k,v in pairs(info) do PRINT(k,v) end

    assert( info.nups == (_VERSION == "Lua 5.1" and 2 or 3) )    -- one upvalue + PRINT + _ENV (Lua 5.2 only)
    assert( info.what == "Lua" )
    --assert( info.name == "chunk2" )   -- name does not seem to come through
    assert( string.match( info.source, "^@.*basic.lua$" ) )
    assert( string.match( info.short_src, "^.*basic.lua$" ) )
    -- These vary so let's not be picky (they're there..)
    --
    assert( info.linedefined > 200 )   -- start of 'chunk2'
    assert( info.currentline > info.linedefined )   -- line of 'debug.getinfo'
    assert( info.lastlinedefined > info.currentline )   -- end of 'chunk2'
    local k,func= linda:receive( "down" )
    assert( type(func)=="function" )
    assert( k=="down" )

    func(linda)

    local k,str= linda:receive( "down" )
    assert( str=="ok" )

    linda:send( "up", function() return ":)" end, "ok2" )
end

local linda= lanes.linda()
local t2= lanes_gen( "debug,string,io", {gc_cb = gc_cb}, chunk2 )(linda)     -- prepare & launch
linda:send( "down", function(linda) linda:send( "up", "ready!" ) end,
                    "ok" )
-- wait to see if the tiny function gets executed
--
local k,s= linda:receive( 1, "up" )
if t2.status == "error" then
	print( "t2 error: " , t2:join())
end
PRINT(s)
assert( s=="ready!" )

-- returns of the 'chunk2' itself
--
local k,f= linda:receive( "up" )
assert( type(f)=="function" )

local s2= f()
assert( s2==":)" )

local k,ok2= linda:receive( "up" )
assert( ok2 == "ok2" )

--##############################################################
--##############################################################
--##############################################################

PRINT( "\n\n", "---=== :join test ===---", "\n\n")

-- NOTE: 'unpack()' cannot be used on the lane handle; it will always return nil
--       (unless [1..n] has been read earlier, in which case it would seemingly
--       work).

local S= lanes_gen( "table", {gc_cb = gc_cb},
  function(arg)
		set_debug_threadname "join test lane"
		set_finalizer( function() end)
    aux= {}
    for i, v in ipairs(arg) do
	   table.insert (aux, 1, v)
    end
		-- unpack was renamed table.unpack in Lua 5.2: cater for both!
    return (unpack or table.unpack)(aux)
end )

h= S { 12, 13, 14 }     -- execution starts, h[1..3] will get the return values
-- wait a bit so that the lane hasa chance to set its debug name
linda:receive(0.5, "gloupti")
print( "joining with '" .. h:get_debug_threadname() .. "'")
local a,b,c,d= h:join()
if h.status == "error" then
	print( h:get_debug_threadname(), "error: " , a, b, c, d)
else
	print( h:get_debug_threadname(), a,b,c,d)
	assert(a==14)
	assert(b==13)
	assert(c==12)
	assert(d==nil)
end

--
io.stderr:write "Done! :)\n"
