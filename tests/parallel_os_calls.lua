local lanes = require "lanes".configure{with_timers = false, nb_keepers = 1}
print( os.date())
local linda = lanes.linda()
local l1 = lanes.gen("os,base", function() print "start sleeping" linda:receive(10, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() print "start sleeping" linda:receive(9, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() print "start sleeping" linda:receive(9, "null") print("finished_sleeping " .. os.date()) end)()
lanes.gen("os,base", function() print "start sleeping" linda:receive(9, "null") print("finished_sleeping " .. os.date()) end)()
-- wait, else all lanes will get hard-cancelled at stat shutdown
l1:join()
--[[
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
lanes.gen("os,base", function() os.execute('sleep 10 && echo finished_sleeping') print( os.date()) end)()
]]
