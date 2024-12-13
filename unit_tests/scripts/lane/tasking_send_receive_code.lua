local config = { with_timers = false, strip_functions = false, internal_allocator = "libc"}
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

local gc_cb = function(name_, status_)
    PRINT("				---> lane '" .. name_ .. "' collected with status '" .. status_ .. "'")
end

-- ##################################################################################################
-- ##################################################################################################
-- ##################################################################################################

PRINT("---=== Receive & send of code ===---", "\n")

local upvalue = "123"
local tostring = tostring

local function chunk2(linda)
    local utils = require "_utils"
    local PRINT = utils.MAKE_PRINT()
    PRINT("here")
    assert(upvalue == "123")    -- even when running as separate thread
    -- function name & line number should be there even as separate thread
    --
    local info= debug.getinfo(1)    -- 1 = us
    --
    PRINT("linda named-> '" ..tostring(linda).."'")
    PRINT "debug.getinfo->"
    for k,v in pairs(info) do PRINT(k,v) end

    -- some assertions are adjusted depending on config.strip_functions, because it changes what we get out of debug.getinfo
    assert(info.nups == (_VERSION == "Lua 5.1" and 3 or 4), "bad nups " .. info.nups)    -- upvalue + config + tostring + _ENV (Lua > 5.2 only)
    assert(info.what == "Lua", "bad what")
    --assert(info.name == "chunk2")   -- name does not seem to come through
    assert(config.strip_functions and info.source=="=?" or string.match(info.source, "^@.*tasking_send_receive_code.lua$"), "bad info.source " .. info.source)
    assert(config.strip_functions and info.short_src=="?" or string.match(info.short_src, "^.*tasking_send_receive_code.lua$"), "bad info.short_src " .. info.short_src)
    -- These vary so let's not be picky (they're there..)
    --
    assert(info.linedefined == 32, "bad linedefined")   -- start of 'chunk2'
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
local t2= lanes_gen("debug,package,string,io", {gc_cb = gc_cb}, chunk2)(linda)     -- prepare & launch
linda:send("down", function(linda) linda:send("up", "ready!") end,
                    "ok")
-- wait to see if the tiny function gets executed
--
local k,s= linda:receive(1, "up")
if t2.status == "error" then
    PRINT("t2 error: " , t2:join())
    assert(false)
end
PRINT(s)
assert(s=="ready!", s .. " is not 'ready!'")

-- returns of the 'chunk2' itself
--
local k,f= linda:receive("up")
assert(type(f)=="function")

local s2= f()
assert(s2==":)")

local k,ok2= linda:receive("up")
assert(ok2 == "ok2")
