local require_lanes_result_1, require_lanes_result_2 = require "lanes".configure(config).configure()
print("require_lanes_result:", require_lanes_result_1, require_lanes_result_2)
local lanes = require_lanes_result_1

local require_assert_result_1, require_assert_result_2 = require "_assert"
print("require_assert_result:", require_assert_result_1, require_assert_result_2)

local utils = lanes.require "_utils"
local PRINT = utils.MAKE_PRINT()

local lanes_gen = assert(lanes.gen)
local lanes_linda = assert(lanes.linda)

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

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

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

local tables_match = utils.tables_match

local SLEEP = function(...)
    local k, v = lanes.sleep(...)
    assert(k == nil and v == "timeout")
end

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
