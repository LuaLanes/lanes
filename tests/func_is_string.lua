local lanes = require "lanes".configure()

-- Lua 5.4+ specific:
if _VERSION >= "Lua 5.4" then
    -- go through a string so that the script we are in doesn't trigger a parse error with older Lua
    local res = assert(load [[
        local lanes = require 'lanes'
        local r
        do
            local h <close> = lanes.gen('*', { name = 'auto' }, lanes.sleep)(0.5)
            r = h
        end -- h is closed here
        return r.status
    ]])()
    -- when the do...end block is exited, the to-be-closed variable h is closed, which internally calls h:join()
    -- therefore the status of the lane should be "done"
    assert(res == "done", "got " .. tostring(res))
end

-- TEST: a string with a parse error is handled properly
do
    -- the generator does not compile the string yet
    local g = lanes.gen("*", { name = 'auto' }, "retrun true")
    -- invoking the generator compiles the string
    local s, r = pcall(g)
    assert(s == false and type(r) == 'string' and string.find(r, "error when parsing lane function code"), "got " .. r)
end

-- TEST: lane executes fine, raises the expected error
do
    local g = lanes.gen("*", { name = 'auto' }, "return true, error('bob')")
    fibLane = g()
    lanes.sleep(0.1)
    assert(fibLane.status == "error")
    local _r, _err, _stk = fibLane:join()
    assert(_r == nil and _err == [[[string "return true, error('bob')"]:1: bob]], "got " .. tostring(_r) .. " " .. tostring(_err) .. " " .. tostring(_stk))
end

-- TEST: lanes execute and return the expected value obtained from the provided globals
do
    local options = {globals = { b = 666 }}
    local g = lanes.gen(options, { name = 'auto' }, "return b")
    local retLane1, retLane2 = g(), g()
    assert(retLane1[1] == 666 and retLane2[1] == 666)
end
