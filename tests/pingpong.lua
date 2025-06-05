local lanes = require "lanes"
local q = lanes.linda()

local pingpong = function(name, qr, qs, start)
    print("start " .. name, qr, qs, start)
    local count = 0
    if start then
        print(name .. ": sending " .. qs .. " 0")
        q:send(qs, 0)
    end
    while count < 10 do
        print(name .. ": receiving " .. qr)
        local key, val = q:receive(qr)
        if val == nil then
            print(name .. ": timeout")
            break
        end
        print(name .. ":" .. val)
        val = val + 1
        print(name .. ": sending " .. qs .. " " .. tostring(val + 1))
        q:send(qs, val)
        count = count + 1
    end
    return "ping!"
end

-- pingpong("L1", '0', '1', true)
local t1, err1 = lanes.gen("*", { name = 'auto' }, pingpong)("L1", 'a', 'b', true)
local t2, err2 = lanes.gen("*", { name = 'auto' }, pingpong)("L2", 'b', 'a', false)

local r1, ret1 = t1:join()
assert(r1 == true and ret1 == "ping!")
local r2, ret2 = t2:join()
assert(r2 == true and ret2 == "ping!")
print "TEST OK"
