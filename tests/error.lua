--
-- Error reporting
--
local function PRINT(...)
    local str=""
    for i=1,select('#',...) do
        str= str..tostring(select(i,...)).."\t"
    end
    if io then
        io.stderr:write(str.."\n")
    end
end

local which_tests, remaining_tests = {}, {}
for k,v in ipairs{...} do
    PRINT("got arg:", type(v), tostring(v))
    which_tests[v] = true
    remaining_tests[v] = true
end

--##################################################################################################

local lanes = require "lanes".configure{with_timers = false}
local null = lanes.null

--##################################################################################################

-- Lua51 support
local table_unpack = unpack or table.unpack

--##################################################################################################

local WR = function(...)
    local str=""
    for i=1,select('#',...) do
        local v = select(i,...)
        if type(v) == "function" then
            local infos = debug.getinfo(v)
            --[[for k,v in pairs(infos) do
                PRINT(k,v)
            end]]
            v = infos.source..":"..infos.linedefined
        end
        str= str..tostring(v).."\t"
    end
    if io then
        io.stderr:write(str.."\n")
    end
end

--##################################################################################################

local lane_body = function(error_value_, finalizer_, finalizer_error_value_)
    WR( "In Lane body: EV: ", error_value_, " F: ", finalizer_, " FEV: ", finalizer_error_value_)
    if finalizer_ then
        local finalizer = function(err_, stack_)
            finalizer_(err_, stack_)
            if finalizer_error_value_ then
                WR ("Finalizer raises ", finalizer_error_value_)
                error(finalizer_error_value_, 0) -- 0 so that error() doesn't alter the error value
            end
        end
        set_finalizer(finalizer)
    end

    local subf = function()  -- this so that we can see the call stack
        if error_value_ then
            error(error_value_, 0) -- 0 so that error() doesn't alter the error value
        end
        return "success"
    end
    local subf2 = function(b_)
        return b_ or subf() -- prevent tail call
    end
    local subf3 = function(b_)
        return b_ or subf2() -- prevent tail call
    end
    return subf3(false)
end

--##################################################################################################

local lane_finalizer = function(err_, stack_)
    WR("In finalizer: ", err_, stack_)
end

--##################################################################################################

local start_lane = function(error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_)
    return lanes.gen("*", {error_trace_level = error_reporting_mode_}, lane_body)(error_value_, finalizer_, finalizer_error_value_)
end

--##################################################################################################
--##################################################################################################

local make_table_error_mt = function()
    return {
        __tostring = function() return "{error as table}" end,
        __eq = function(t1_, t2_)
            -- because tables transfered through linda/error reporting are identical, but not equal
            -- however, due to metatable caching by Lanes, their metatables should be the same
            return getmetatable(t1_) == getmetatable(t2_)
        end
    }
end

local lane_error_as_string = "'lane error as string'"
local lane_error_as_table = setmetatable({"lane error as table"}, make_table_error_mt())
local lane_error_as_linda = lanes.linda("'lane error'")

local finalizer_error_as_string = "'finalizer error as string'"
local finalizer_error_as_table = setmetatable({"finalizer error as table"}, make_table_error_mt())
local finalizer_error_as_linda = lanes.linda("'finalizer error'")

local test_settings = {}
local configure_tests = function()
    local append_test = function(level, lane_error, finalizer, finalizer_error)
        -- convert back null to nil
        lane_error = lane_error ~= null and lane_error or nil
        finalizer = finalizer ~= null and finalizer or nil
        finalizer_error = finalizer_error ~= null and finalizer_error or nil
        -- compose test description string
        local test_header = table.concat({
            level .. " error reporting",
            (lane_error and tostring(lane_error) or "no error") .. " in lane",
            (finalizer and "with" or "without").. " finalizer" .. ((finalizer and finalizer_error) and " raising " .. tostring(finalizer_error) or "")
        }, ", ")
        PRINT(test_header)
        test_settings[test_header] = { level, lane_error, finalizer, finalizer_error }
    end
    -- can't store nil in tables, use null instead
    local levels = {
        "minimal",
        "basic",
        "extended",
    }
    local errors = {
        null,
        lane_error_as_string,
        lane_error_as_table,
        lane_error_as_linda,
    }
    local finalizers = {
        null,
        lane_finalizer,
    }
    local finalizer_errors = {
        null,
        finalizer_error_as_string,
        finalizer_error_as_table,
        finalizer_error_as_linda,
    }
 
    for _, level in ipairs(levels) do
        for _, lane_error in ipairs(errors) do
            for _, finalizer in ipairs(finalizers) do
                for _, finalizer_error in ipairs(finalizer_errors) do
                    append_test(level, lane_error, finalizer, finalizer_error)
                end
            end
        end
    end
    WR "Tests configured"
end

configure_tests()
-- do return end

--##################################################################################################
--##################################################################################################

local do_error_catching_test = function(error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_)
    local h = start_lane(error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_)
    local ret,err,stack= h:join()   -- wait for the lane (no automatic error propagation)
    WR("Processing results for {", error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_, "}")
    if err then
        assert(ret == nil)
        assert(error_reporting_mode_ == "minimal" or type(stack)=="table") -- only true if lane was configured with error_trace_level ~= "minimal"
        if err == error_value_ then
            WR("Lane regular error: ", err)
        elseif err == finalizer_error_value_ then
            WR("Lane finalizer error: ", err)
        else
            WR("Unknown error: ", type(err), err)
            assert(false)
        end
        if error_reporting_mode_ == "minimal" then
            assert(stack == nil, table.concat{"stack is a ", type(stack)})
        elseif error_reporting_mode_ == "basic" then -- each stack line is a string in basic mode
            WR("STACK:\n", table.concat(stack,"\n\t"));
        else -- each stack line is a table in extended mode
            WR "STACK:"
            for line, details in pairs(stack) do
                WR("\t", line);
                for detail, value in pairs(details) do
                    WR("\t\t", detail, ": ", value)
                end
            end
        end
    else -- no error
        assert(ret == "success")
        WR("No error in lane: ", ret)
    end
    WR "TEST OK"
end

--##################################################################################################

local do_error_propagation_test = function(error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_)
    local wrapper = function()
        local raises_error = (error_value_ or finalizer_error_value_) and true or false
        local h = start_lane(error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_)
        local _ = h[0]
        -- if the lane is configured to raise an error, we should not get here
        assert(not raises_error, "should not get here")
    end
    local err, msg = pcall(wrapper)
    WR("Processing results for {", error_reporting_mode_, error_value_, finalizer_, finalizer_error_value_, "}")
    if err then
        WR("Lane error: ", msg)
    end
    WR "TEST OK"
end

--##################################################################################################

local perform_test = function(title_, test_)
    WR "###########################################################################"
    WR ("** " .. title_ .. " **")
    for desc, settings in pairs(test_settings) do
        WR "---------------------------------------------------------------------------"
        WR(desc)
        test_(table_unpack(settings))
    end
end

if not next(which_tests) or which_tests.catch then
    remaining_tests.catch = nil
    perform_test("Error catching", do_error_catching_test)
end

--##################################################################################################
if not next(which_tests) or which_tests.propagate then
    remaining_tests.propagate = nil
    perform_test("Error propagation", do_error_propagation_test)
end

-- ##################################################################################################

local unknown_test, val = next(remaining_tests)
assert(not unknown_test, tostring(unknown_test) .. " test is unknown")

PRINT "\nTHE END"
