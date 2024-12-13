local lanes = require "lanes"

-- a newly created linda doesn't contain anything
local l = lanes.linda()
assert(l.null == lanes.null)
assert(l:dump() == nil)

-- read something with 0 timeout, should fail
local k,v = l:receive(0, "k")
assert(k == nil and v == 'timeout')

-- send some value
assert(l:send("k1", 99) == true)
-- make sure the contents look as expected
local t = l:dump()
assert(type(t) == 'table')
local tk1 = t.k1
assert(type(tk1) == 'table')
assert(tk1.first == 1 and tk1.count == 1 and tk1.limit == 'unlimited' and tk1.restrict == 'none')
assert(#tk1.fifo == tk1.count, #tk1.fifo .. " ~= " .. tk1.count)
assert(tk1.fifo[1] == 99, tk1.fifo[1] .. " ~= " .. 99)

-- read the value, should succeed
local k,v = l:receive("k1")
assert(k == "k1" and v == 99)
-- after reading, the data is no longer available (even if the key itself still exists within the linda)
local t = l:dump()
assert(type(t) == 'table')
local tk1 = t.k1
assert(type(tk1) == 'table')
assert(tk1.first == 1 and tk1.count == 0 and tk1.limit == 'unlimited' and tk1.restrict == 'none')
assert(#tk1.fifo == tk1.count and tk1.fifo[1] == nil)
-- read again, should fail
local k,v = l:receive(0, "k1")
assert(k == nil and v == 'timeout')

-- send null, read nil
l:send("k", l.null)
local k,v = l:receive(0, "k")
assert(k == "k" and v == nil)

-- using a deep userdata (such as another linda) as key, should work
local l2 = lanes.linda()
l:send(nil, l2, 99)
local k,v = l:receive(0, l2)
assert(k == l2 and v == 99)
