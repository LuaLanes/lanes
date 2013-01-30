local lanes = require 'lanes'.configure()
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
end

-- pingpong("L1", '0', '1', true)
local t1, err1 = lanes.gen("*", pingpong)("L1", 'a', 'b', true)
local t2, err2 = lanes.gen("*", pingpong)("L2", 'b', 'a', false)

t1:join()
t2:join()