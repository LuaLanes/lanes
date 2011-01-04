--
-- APPENDUD.LUA
--
-- Lanes version for John Belmonte's challenge on Lua list (about finalizers):
-- <http://lua-users.org/lists/lua-l/2008-02/msg00243.html>
--
-- Needs Lanes >= 2.0.3
--
require "lanes"

local _tab = {
    beginupdate = function (this) print('tab.beginupdate') end;
    endupdate = function (this) print('tab.endupdate') end;
}
local _ud = {
    lock = function (this) print('ud.lock') end;
    unlock = function (this) print('ud.unlock') end;
    1,2,3,4,5;
}

-- 
-- This sample is with the 'finalize/guard' patch applied (new keywords):
--
--function appendud(tab, ud)
--    tab:beginupdate() finalize tab:endupdate() end
--    ud:lock() finalize ud:unlock() end
--    for i = 1,#ud do
--        tab[#tab+1] = ud[i]
--    end
--end


function appendud(tab, ud)
io.stderr:write "Starting"
    tab:beginupdate() set_finalizer( function() tab:endupdate() end )
    ud:lock() set_finalizer( function() ud:unlock() end )
    for i = 1,#ud do
        tab[#tab+1] = ud[i]
    end
io.stderr:write "Ending"
    return tab  -- need to return 'tab' since we're running in a separate thread
                -- ('tab' is passed over lanes by value, not by reference)
end

local t,err= lanes.gen( appendud )( _tab, _ud )   -- create & launch a thread
assert(t)
assert(not err)

-- test

t:join()    -- Need to explicitly wait for the thread, since 'ipairs()' does not
            -- value the '__index' metamethod (wouldn't it be cool if it did..?)

io.stderr:write(t[1])

for k,v in ipairs(t) do
    print(k,v)
end
