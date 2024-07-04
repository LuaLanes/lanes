--
-- BASIC.LUA           Copyright (c) 2007-08, Asko Kauppi <akauppi@gmail.com>
--
-- Selftests for Lua Lanes
--
-- To do:
--      - ...
--
local config = { with_timers = false, strip_functions = false, internal_allocator = "libc"}
-- calling configure more than once should work (additional called are ignored)
local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "assert"    -- assert.fails()
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local lanes_gen=    assert(lanes.gen)
local lanes_linda = assert(lanes.linda)

local tostring=     assert(tostring)

local function PRINT(...)
    local str=""
    for i=1,select('#',...) do
        str= str..tostring(select(i,...)).."\t"
    end
    if io then
        io.stderr:write(str.."\n")
    end
end

local SLEEP = function(...)
    local k, v = lanes.sleep(...)
    assert(k == nil and v == "timeout")
end

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end
--gc_cb = nil


---=== Local helpers ===---

local tables_match

-- true if 'a' is a subtable of 'b'
--
local function subtable(a, b)
    --
    assert(type(a)=="table" and type(b)=="table")

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
tables_match= function(a, b)
    return subtable(a, b) and subtable(b, a)
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Tasking (basic) ===---", "\n\n")

local function task(a, b, c)
    lane_threadname("task("..a..","..b..","..c..")")
    --error "111"     -- testing error messages
    assert(hey)
    local v=0
    for i=a,b,c do
        v= v+i
    end
    return v, hey
end

local task_launch= lanes_gen("", { globals={hey=true}, gc_cb = gc_cb}, task)
    -- base stdlibs, normal priority

-- 'task_launch' is a factory of multithreaded tasks, we can launch several:

local lane1= task_launch(100,200,3)
assert.fails(function() print(lane1[lane1]) end) -- indexing the lane with anything other than a string or a number should fail

local lane2= task_launch(200,300,4)

-- At this stage, states may be "pending", "running" or "done"

local st1,st2= lane1.status, lane2.status
PRINT(st1,st2)
assert(st1=="pending" or st1=="running" or st1=="done")
assert(st2=="pending" or st2=="running" or st2=="done")

-- Accessing results ([1..N]) pends until they are available
--
PRINT("waiting...")
local v1, v1_hey= lane1[1], lane1[2]
local v2, v2_hey= lane2[1], lane2[2]

PRINT(v1, v1_hey)
assert(v1_hey == true)

PRINT(v2, v2_hey)
assert(v2_hey == true)

assert(lane1.status == "done")
assert(lane1.status == "done")
lane1, lane2 = nil
collectgarbage()

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Tasking (cancelling) ===---", "\n\n")

local task_launch2= lanes_gen("", { globals={hey=true}, gc_cb = gc_cb}, task)

local N=999999999
local lane9= task_launch2(1,N,1)   -- huuuuuuge...

-- Wait until state changes "pending"->"running"
--
local st
local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="pending" then break end
end
PRINT(" "..st)

if st=="error" then
    local _= lane9[0]  -- propagate the error here
end
if st=="done" then
    error("Looping to "..N.." was not long enough (cannot test cancellation)")
end
assert(st=="running")

-- when running under luajit, the function is JIT-ed, and the instruction count isn't hit, so we need a different hook
lane9:cancel(jit and "line" or "count", 100) -- 0 timeout, hook triggers cancelslation when reaching the specified count

local t0= os.time()
while os.time()-t0 < 5 do
    st= lane9.status
    io.stderr:write((i==1) and st.." " or '.')
    if st~="running" then break end
end
PRINT(" "..st)
assert(st == "cancelled", "st is '" .. st .. "' instead of 'cancelled'")

-- cancellation of lanes waiting on a linda
local limited = lanes_linda("limited")
assert.fails(function() limited:limit("key", -1) end)
assert.failsnot(function() limited:limit("key", 1) end)
-- [[################################################
limited:send("key", "hello") -- saturate linda
for k, v in pairs(limited:dump()) do
    PRINT("limited[" .. tostring(k) .. "] = " .. tostring(v))
end
local wait_send = function()
    local a,b
    set_finalizer(function() print("wait_send", a, b) end)
    a,b = limited:send("key", "bybye") -- infinite timeout, returns only when lane is cancelled
end

local wait_send_lane = lanes.gen("*", wait_send)()
repeat until wait_send_lane.status == "waiting"
print "wait_send_lane is waiting"
wait_send_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_send_lane.status == "cancelled"
print "wait_send_lane is cancelled"
--################################################]]
local wait_receive = function()
    local k, v
    set_finalizer(function() print("wait_receive", k, v) end)
    k, v = limited:receive("dummy") -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_lane = lanes.gen("*", wait_receive)()
repeat until wait_receive_lane.status == "waiting"
print "wait_receive_lane is waiting"
wait_receive_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_lane.status == "cancelled"
print "wait_receive_lane is cancelled"
--################################################]]
local wait_receive_batched = function()
    local k, v1, v2
    set_finalizer(function() print("wait_receive_batched", k, v1, v2) end)
    k, v1, v2 = limited:receive(limited.batched, "dummy", 2) -- infinite timeout, returns only when lane is cancelled
end

local wait_receive_batched_lane = lanes.gen("*", wait_receive_batched)()
repeat until wait_receive_batched_lane.status == "waiting"
print "wait_receive_batched_lane is waiting"
wait_receive_batched_lane:cancel() -- hard cancel, 0 timeout
repeat until wait_receive_batched_lane.status == "cancelled"
print "wait_receive_batched_lane is cancelled"
--################################################]]

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Communications ===---", "\n\n")

local function WR(...) io.stderr:write(...) end

local chunk= function(linda)
    local function receive() return linda:receive("->") end
    local function send(...) local _res, _err = linda:send("<-", ...) assert(_res == true and _err == nil) end

    WR("chunk ", "Lane starts!\n")

    local k,v
    k,v=receive(); WR("chunk ", v.." received (expecting 1)\n"); assert(k and v==1)
    k,v=receive(); WR("chunk ", v.." received (expecting 2)\n"); assert(k and v==2)
    k,v=receive(); WR("chunk ", v.." received  (expecting 3)\n"); assert(k and v==3)
    k,v=receive(); WR("chunk ", tostring(v).." received  (expecting nil from __lanesconvert)\n"); assert(k and v==nil, "table with __lanesconvert==lanes.null should be received as nil, got " .. tostring(v)) -- a table with __lanesconvert was sent
    k,v=receive(); WR("chunk ", tostring(v).." received (expecting nil)\n"); assert(k and v==nil)

    send(4,5,6);              WR("chunk ", "4,5,6 sent\n")
    send 'aaa';               WR("chunk ", "'aaa' sent\n")
    send(nil);                WR("chunk ", "nil sent\n")
    send { 'a', 'b', 'c', d=10 }; WR("chunk ","{'a','b','c',d=10} sent\n")

    k,v=receive(); WR("chunk ", v.." received\n"); assert(v==4)

    local subT1 = { "subT1"}
    local subT2 = { "subT2"}
    send { subT1, subT2, subT1, subT2}; WR("chunk ", "{ subT1, subT2, subT1, subT2} sent\n")

    WR("chunk ", "Lane ends!\n")
end

local linda = lanes_linda("communications")
assert(type(linda) == "userdata" and tostring(linda) == "Linda: communications")
    --
    -- ["->"] master -> slave
    -- ["<-"] slave <- master

WR "test linda:get/set..."
linda:set("<->", "x", "y", "z")
local b,x,y,z = linda:get("<->", 1)
assert(b == 1 and x == "x" and y == nil and z == nil)
local b,x,y,z = linda:get("<->", 2)
assert(b == 2 and x == "x" and y == "y" and z == nil)
local b,x,y,z = linda:get("<->", 3)
assert(b == 3 and x == "x" and y == "y" and z == "z")
local b,x,y,z,w = linda:get("<->", 4)
assert(b == 3 and x == "x" and y == "y" and z == "z" and w == nil)
local k, x = linda:receive("<->")
assert(k == "<->" and x == "x")
local k,y,z = linda:receive(linda.batched, "<->", 2)
assert(k == "<->" and y == "y" and z == "z")
linda:set("<->")
local b,x,y,z,w = linda:get("<->", 4)
assert(b == 0 and x == nil and y == nil and z == nil and w == nil)
WR "ok\n"

local function PEEK(...) return linda:get("<-", ...) end
local function SEND(...) local _res, _err = linda:send("->", ...) assert(_res == true and _err == nil) end
local function RECEIVE() local k,v = linda:receive(1, "<-") return v end

local comms_lane = lanes_gen("io", {gc_cb = gc_cb, name = "auto"}, chunk)(linda)     -- prepare & launch

SEND(1);  WR("main ", "1 sent\n")
SEND(2);  WR("main ", "2 sent\n")
SEND(3);  WR("main ", "3 sent\n")
SEND(setmetatable({"should be ignored"},{__lanesconvert=lanes.null})); WR("main ", "__lanesconvert table sent\n")
for i=1,40 do
    WR "."
    SLEEP(0.0001)
    assert(PEEK() == 0)    -- nothing coming in, yet
end
SEND(nil);  WR("\nmain ", "nil sent\n")

local a,b,c = RECEIVE(), RECEIVE(), RECEIVE()

print("lane status: " .. comms_lane.status)
if comms_lane.status == "error" then
    print(comms_lane:join())
else
    WR("main ", tostring(a)..", "..tostring(b)..", "..tostring(c).." received\n")
end

assert(a==4 and b==5 and c==6)

local aaa = RECEIVE(); WR("main ", aaa.." received\n")
assert(aaa=='aaa')

local null = RECEIVE();   WR(tostring(null).." received\n")
assert(null==nil)

local out_t = RECEIVE();   WR(type(out_t).." received\n")
assert(tables_match(out_t, {'a','b','c',d=10}))

assert(PEEK() == 0)
SEND(4)

local complex_table = RECEIVE(); WR(type(complex_table).." received\n")
assert(complex_table[1] == complex_table[3] and complex_table[2] == complex_table[4])
WR(table.concat({complex_table[1][1],complex_table[2][1],complex_table[3][1],complex_table[4][1]},", "))

WR("collectgarbage")
comms_lane = nil
collectgarbage()
-- wait
WR("waiting 1s")
SLEEP(1)

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Stdlib naming ===---", "\n\n")

local function dump_g(_x)
    lane_threadname "dump_g"
    assert(print)
    print("### dumping _G for '" .. _x .. "'")
    for k, v in pairs(_G) do
        print("\t" .. k .. ": " .. type(v))
    end
    return true
end

local function io_os_f(_x)
    lane_threadname "io_os_f"
    assert(print)
    print("### checking io and os libs existence for '" .. _x .. "'")
    assert(io)
    assert(os)
    return true
end

local function coro_f(_x)
    lane_threadname "coro_f"
    assert(print)
    print("### checking coroutine lib existence for '" .. _x .. "'")
    assert(coroutine)
    return true
end

assert.fails(function() lanes_gen("xxx", {gc_cb = gc_cb}, io_os_f) end)

local stdlib_naming_tests =
{
    -- { "", dump_g},
    -- { "coroutine", dump_g},
    -- { "io", dump_g},
    -- { "bit32", dump_g},
    { "coroutine?", coro_f}, -- in Lua 5.1, the coroutine base library doesn't exist (coroutine table is created when 'base' is opened)
    { "*", io_os_f},
    { "io,os", io_os_f},
    { "io+os", io_os_f},
    { "io,os,base", io_os_f},
}

for _, t in ipairs(stdlib_naming_tests) do
    local f= lanes_gen(t[1], {gc_cb = gc_cb}, t[2])     -- any delimiter will do
    assert(f(t[1])[1])
end

WR("collectgarbage")
collectgarbage()

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Comms criss cross ===---", "\n\n")

-- We make two identical lanes, which are using the same Linda channel.
--
local tc= lanes_gen("io", {gc_cb = gc_cb},
  function(linda, ch_in, ch_out)
        lane_threadname("criss cross " .. ch_in .. " -> " .. ch_out)
    local function STAGE(str)
        io.stderr:write(ch_in..": "..str.."\n")
        linda:send(nil, ch_out, str)
        local k,v= linda:receive(nil, ch_in)
        assert(v==str)
    end
    STAGE("Hello")
    STAGE("I was here first!")
    STAGE("So what?")
  end
)

local linda= lanes_linda("criss cross")

local a,b= tc(linda, "A","B"), tc(linda, "B","A")   -- launching two lanes, twisted comms

local _= a[1],b[1]  -- waits until they are both ready

WR("collectgarbage")
a, b = nil
collectgarbage()

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== Receive & send of code ===---", "\n\n")

local upvalue="123"

local function chunk2(linda)
    assert(upvalue=="123")    -- even when running as separate thread
    -- function name & line number should be there even as separate thread
    --
    local info= debug.getinfo(1)    -- 1 = us
    --
    PRINT("linda named-> '" ..tostring(linda).."'")
    PRINT "debug.getinfo->"
    for k,v in pairs(info) do PRINT(k,v) end

    -- some assertions are adjusted depending on config.strip_functions, because it changes what we get out of debug.getinfo
    assert(info.nups == (_VERSION == "Lua 5.1" and 4 or 5), "bad nups " .. info.nups)    -- upvalue + config + PRINT + tostring + _ENV (Lua > 5.2 only)
    assert(info.what == "Lua", "bad what")
    --assert(info.name == "chunk2")   -- name does not seem to come through
    assert(config.strip_functions and info.source=="=?" or string.match(info.source, "^@.*basic.lua$"), "bad info.source")
    assert(config.strip_functions and info.short_src=="?" or string.match(info.short_src, "^.*basic.lua$"), "bad info.short_src")
    -- These vary so let's not be picky (they're there..)
    --
    assert(info.linedefined == 422, "bad linedefined")   -- start of 'chunk2'
    assert(config.strip_functions and info.currentline==-1 or info.currentline > info.linedefined, "bad currentline")   -- line of 'debug.getinfo'
    assert(info.lastlinedefined > info.currentline, "bad lastlinedefined")   -- end of 'chunk2'
    local k,func= linda:receive("down")
    assert(type(func)=="function", "not a function")
    assert(k=="down")

    func(linda)

    local k,str= linda:receive("down")
    assert(str=="ok", "bad receive result")

    linda:send("up", function() return ":)" end, "ok2")
end

local linda = lanes_linda("auto")
local t2= lanes_gen("debug,string,io", {gc_cb = gc_cb}, chunk2)(linda)     -- prepare & launch
linda:send("down", function(linda) linda:send("up", "ready!") end,
                    "ok")
-- wait to see if the tiny function gets executed
--
local k,s= linda:receive(1, "up")
if t2.status == "error" then
    print("t2 error: " , t2:join())
end
PRINT(s)
assert(s=="ready!")

-- returns of the 'chunk2' itself
--
local k,f= linda:receive("up")
assert(type(f)=="function")

local s2= f()
assert(s2==":)")

local k,ok2= linda:receive("up")
assert(ok2 == "ok2")

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("\n\n", "---=== :join test ===---", "\n\n")

-- NOTE: 'unpack()' cannot be used on the lane handle; it will always return nil
--       (unless [1..n] has been read earlier, in which case it would seemingly
--       work).

local S= lanes_gen("table", {gc_cb = gc_cb},
  function(arg)
        lane_threadname "join test lane"
        set_finalizer(function() end)
    aux= {}
    for i, v in ipairs(arg) do
       table.insert (aux, 1, v)
    end
        -- unpack was renamed table.unpack in Lua 5.2: cater for both!
    return (unpack or table.unpack)(aux)
end)

h= S { 12, 13, 14 }     -- execution starts, h[1..3] will get the return values
-- wait a bit so that the lane has a chance to set its debug name
SLEEP(0.5)
print("joining with '" .. h:get_threadname() .. "'")
local a,b,c,d= h:join()
if h.status == "error" then
    print(h:get_threadname(), "error: " , a, b, c, d)
else
    print(h:get_threadname(), a,b,c,d)
    assert(a==14)
    assert(b==13)
    assert(c==12)
    assert(d==nil)
end

local nameof_type, nameof_name = lanes.nameof(print)
PRINT("name of " .. nameof_type .. " print = '" .. nameof_name .. "'")
-- install a finalizer that gets called upon Lanes's internal Universe is GCed.
-- that way, we print our message after anything that can be output by lanes that are still running at that point
lanes.finally(function() io.stderr:write "\n=======================================\nTEST OK\n" end)
