--
-- Test resource cleanup
--
-- This feature was ... by discussion on the Lua list about exceptions.
-- The idea is to always run a certain block at exit, whether due to success
-- or error. Normally, 'pcall' would be used for such but as Lua already
-- does that, simply giving a 'cleanup=function' parameter is a logical
-- thing to do.     -- AKa 22-Jan-2009
--

local lanes = require "lanes"
lanes.configure{with_timers=false}
local finally = lanes.finally

local FN = "finalizer-test.tmp"

local cleanup

local function lane(error_)
    set_finalizer(cleanup)

    local st,err = pcall(finally, cleanup) -- should cause an error because called from a lane
    assert(not st, "finally() should have thrown an error")
    io.stderr:write("finally() raised error '", err, "'\n")

    local f,err = io.open(FN,"w")
    if not f then
        error( "Could not create "..FN..": "..err)
    end

    f:write( "Test file that should get removed." )

    io.stderr:write( "File "..FN.." created\n" )
    -- don't forget to close the file immediately, else we won't be able to delete it until f is collected
    f:close()

    if error_ then
        io.stderr:write("Raising ", tostring(error_), "\n")
        error(error_, 0)    -- exception here; the value needs NOT be a string
    end
    -- no exception
    io.stderr:write("Lane completed\n")
    return true
end

-- 
-- This is called at the end of the lane; whether succesful or not.
-- Gets the 'error()' argument as argument ('nil' if normal return).
--
cleanup = function(err)
    io.stderr:write "------------------------ In Worker Finalizer -----------------------\n"
    -- An error in finalizer will override an error (or success) in the main
    -- chunk.
    --
    --error( "This is important!" )

    if err then
        io.stderr:write( "Cleanup after error: "..tostring(err).."\n" )
    else
        io.stderr:write( "Cleanup after normal return\n" )
    end
        
    local _,err2 = os.remove(FN)
    io.stderr:write( "file removal result: ", tostring(err2), "\n")
    assert(not err2)    -- if this fails, it will be shown in the calling script
                        -- as an error from the lane itself
    
    io.stderr:write( "Removed file "..FN.."\n" )
end

-- we need error_trace_level above "minimal" to get a stack trace out of h:join()
local lgen = lanes.gen("*", {error_trace_level = "basic"}, lane)

local do_test = function(error_)

    io.stderr:write "======================== Launching the lane! =======================\n"

    local h = lgen(error_)

    local _,err,stack = h:join()   -- wait for the lane (no automatic error propagation)
    if err then
        assert(stack, "no stack trace on error, check 'error_trace_level'")
        io.stderr:write( "Lane error: "..tostring(err).."\n" )
        io.stderr:write( "\t", table.concat(stack,"\t\n"), "\n" )
    else
        io.stderr:write( "Done\n" )
    end
end

do_test(nil)
-- do return end
do_test("An error")

local on_exit = function()
    finally(nil)
    io.stderr:write "=========================In Lanes Finalizer! =======================\n"
    local f = io.open(FN,"r")
    if f then
        error( "CLEANUP DID NOT WORK: "..FN.." still exists!" )
    else
        io.stderr:write(FN .. " was successfully removed\n")
    end
    io.stderr:write "Finished!\n"
end

-- this function is called after script exit, when the state is closed
lanes.finally(on_exit)
