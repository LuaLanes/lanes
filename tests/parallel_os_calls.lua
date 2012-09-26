local lanes = require "lanes".configure(1)
print( os.date())
local linda = lanes.linda()
lanes.gen("os,base", function() linda:receive(10, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() linda:receive(10, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() linda:receive(10, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() linda:receive(10, "null") print("finished_sleeping " .. os.date()) end)()

--[[
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
]]
