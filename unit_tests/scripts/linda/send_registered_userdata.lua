local lanes = require 'lanes'.configure{with_timers = false}
local l = lanes.linda'gleh'
l:set('yo', io.stdin)
local n, stdin_out = l:get('yo')
assert(n == 1 and stdin_out == io.stdin, tostring(stdin_out) .. " ~= " .. tostring(io.stdin))
