--
-- LANES.LUA
--
-- Multithreading and -core support for Lua
--
-- Authors: Asko Kauppi <akauppi@gmail.com>
--          Benoit Germain <bnt.germain@gmail.com>
--
-- History: see CHANGES
--
--[[
===============================================================================

Copyright (C) 2007-10 Asko Kauppi <akauppi@gmail.com>
Copyright (C) 2010-24 Benoit Germain <bnt.germain@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

===============================================================================
]]--

local core = require "lanes.core"
-- Lua 5.1: module() creates a global variable
-- Lua 5.2: module() is gone
-- almost everything module() does is done by require() anyway
-- -> simply create a table, populate it, return it, and be done
local lanes = {}

-- #################################################################################################

-- Cache globals for code that might run under sandboxing
-- Making copies of necessary system libs will pass them on as upvalues;
-- only the first state doing "require 'lanes'" will need to have 'string'
-- and 'table' visible.
--
local assert = assert(assert)
local error = assert(error)
local pairs = assert(pairs)
local string = assert(string, "'string' library not available")
local string_find = assert(string.find)
local string_gmatch = assert(string.gmatch)
local string_format = assert(string.format)
local select = assert(select)
local setmetatable = assert(setmetatable)
local table = assert(table, "'table' library not available")
local table_insert = assert(table.insert)
local tonumber = assert(tonumber)
local tostring = assert(tostring)
local type = assert(type)

-- #################################################################################################

-- for error reporting when debugging stuff
--[[
local io = assert(io, "'io' library not available")
local function WR(str)
    io.stderr:write(str.."\n" )
end

-- #################################################################################################

local function DUMP(tbl)
    if not tbl then return end
    local str=""
    for k,v in pairs(tbl) do
        str= str..k.."="..tostring(v).."\n"
    end
    WR(str)
end
]]

-- #################################################################################################

local isLuaJIT = (package and package.loaded.jit and jit.version) and true or false

local default_params =
{
    -- LuaJIT provides a thread-unsafe allocator by default, so we need to protect it when used in parallel lanes
    allocator = isLuaJIT and "protected" or nil,
    -- it looks also like LuaJIT allocator may not appreciate direct use of its allocator for other purposes than the VM operation
    internal_allocator = isLuaJIT and "libc" or "allocator",
    keepers_gc_threshold = -1,
    nb_user_keepers = 0,
    on_state_create = nil,
    shutdown_timeout = 0.25,
    strip_functions = true,
    track_lanes = false,
    verbose_errors = false,
    with_timers = false,
}

-- #################################################################################################

local boolean_param_checker = function(val_)
    -- non-'boolean-false|nil' should be 'boolean-true'
    if (not val_) or (val_ == true) then
        return true
    end
    return nil, "not a boolean"
end

local param_checkers =
{
    allocator = function(val_)
        -- can be nil, "protected", or a function
        if val_ ~= nil and val_ ~= "protected" and type(val_) ~= "function" then
            return nil, "unknown value"
        end
        return true
    end,
    internal_allocator = function(val_)
        -- can be "libc" or "allocator"
        if type(val_) ~= "string" then
            return nil, "not a string"
        end
        if val_ ~= "libc" and val_ ~= "allocator" then
            return nil, "unknown value"
        end
        return true
    end,
    keepers_gc_threshold = function(val_)
        -- keepers_gc_threshold should be a number
        if type(val_) ~= "number" then
            return nil, "not a number"
        end
        return true
    end,
    nb_user_keepers = function(val_)
        -- nb_user_keepers should be a number in [0,100] (so that nobody tries to run OOM by specifying a huge amount)
        if type(val_) ~= "number" then
            return nil, "not a number"
        end
        if val_ < 0 or val_ > 100 then
            return nil, "value out of range"
        end
        return true
    end,
    on_state_create = function(val_)
        -- on_state_create may be nil or a function
        if val_ ~= nil and type(val_) ~= "function" then
            return nil, "not a function"
        end
        return true
    end,
    shutdown_timeout = function(val_)
        -- shutdown_timeout should be a number in [0,3600]
        if type(val_) ~= "number" then
            return nil, "not a number"
        end
        if val_ < 0 or val_ > 3600 then
            return nil, "value out of range"
        end
        return true
    end,
    strip_functions = boolean_param_checker,
    track_lanes = boolean_param_checker,
    verbose_errors = boolean_param_checker,
    with_timers = boolean_param_checker,
}

-- #################################################################################################

local params_checker = function(user_settings_)
    if not user_settings_ then
        return default_params
    end
    -- make a copy of the table to leave the provided one unchanged, *and* to help ensure it won't change behind our back
    local _compound_settings = {}
    if type(user_settings_) ~= "table" then
        error "Bad argument #1 to lanes.configure(), should be a table"
    end
    -- any setting unknown to Lanes raises an error
    for _setting, _value in pairs(user_settings_) do
        if not param_checkers[_setting] then
        error("Unknown setting [" .. tostring(_setting) .. "] = " .. tostring(_value))
        end
    end
    -- any setting not present in the provided parameters takes the default value
    for _key, _checker in pairs(param_checkers) do
        local my_param = user_settings_[_key]
        local param
        if my_param ~= nil then
            param = my_param
        else
            param = default_params[_key]
        end
        local _result, _msg = _checker(param)
        if not _result then
            error("Bad parameter " .. _key .. ": " .. tostring(param) .. (_msg and " (" .. _msg .. ")" or ""), 2)
        end
        _compound_settings[_key] = param
    end
    return _compound_settings
end

-- #################################################################################################

local valid_libs =
{
    ["base"] = true,
    ["bit"] = true,
    ["bit32"] = true,
    ["coroutine"] = true,
    ["debug"] = true,
    ["ffi"] = true,
    ["io"] = true,
    ["jit"] = true,
    ["math"] = true,
    ["os"] = true,
    ["package"] = true,
    ["string"] = true,
    ["table"] = true,
    ["utf8"] = true,
    --
    ["lanes.core"] = true
}
-- same structure, but contains only the libraries that the current Lua flavor actually supports
local supported_libs

-- #################################################################################################

local raise_option_error = function(name_, tv_, v_)
    error("Bad '" .. name_ .. "' option: " .. tv_ .. " " .. string_format("%q", tostring(v_)), 4)
end

-- #################################################################################################

-- must match Lane::ErrorTraceLevel values
local error_trace_levels = {
    minimal = 0,
    basic = 1,
    extended = 2
}

local opt_validators =
{
    gc_cb = function(v_)
        local tv = type(v_)
        return (tv == "function") and v_ or raise_option_error("gc_cb", tv, v_)
    end,
    error_trace_level = function(v_)
        local tv = type(v_)
        return (error_trace_levels[v_] ~= nil) and v_ or raise_option_error("error_trace_level", tv, v_)
    end,
    globals = function(v_)
        local tv = type(v_)
        return (tv == "table") and v_ or raise_option_error("globals", tv, v_)
    end,
    name = function(v_)
        local tv = type(v_)
        return (tv == "string") and v_ or raise_option_error("name", tv, v_)
    end,
    package = function(v_)
        local tv = type(v_)
        return (tv == "table") and v_ or raise_option_error("package", tv, v_)
    end,
    priority = function(v_)
        local tv = type(v_)
        return (tv == "number") and v_ or raise_option_error("priority", tv, v_)
    end,
    required = function(v_)
        local tv = type(v_)
        return (tv == "table") and v_ or raise_option_error("required", tv, v_)
    end
}

-- #############################################################################################
-- ##################################### lanes.gen() ###########################################
-- #############################################################################################

local process_gen_opt = function(...)
    -- aggregrate all strings together, separated by "," as well as tables
    -- the strings are a list of libraries to open
    -- the tables contain the lane options
    local opt = {}
    local libs = nil

    local n = select('#', ...)

    -- we need at least a function
    if n == 0 then
        error("No arguments!", 2)
    end

    -- all arguments but the last must be nil, strings, or tables
    for i = 1, n - 1 do
        local v = select(i, ...)
        local tv = type(v)
        if tv == "string" then
            libs = libs and libs .. "," .. v or v
        elseif tv == "table" then
            for k, vv in pairs(v) do
                opt[k]= vv
            end
        elseif v == nil then
            -- skip
        else
            error("Bad argument " .. i .. ": " .. tv .. " " .. string_format("%q", tostring(v)), 2)
        end
    end

    -- the last argument should be a function or a string
    local func = select(n, ...)
    local functype = type(func)
    if functype ~= "function" and functype ~= "string" then
        error("Last argument not function or string: " .. functype .. " " .. string_format("%q", tostring(func)), 2)
    end

    -- check that the caller only provides reserved library names, and those only once
    -- "*" is a special case that doesn't require individual checking
    if libs and libs ~= "*" then
        if string_find(libs, "*", 2, true) then
            error "Libs specification '*' must be used alone"
        end
        local found = {}
        -- accept lib identifiers followed by an optional question mark
        for s, question in string_gmatch(libs, "([%a%d.]+)(%??)") do
            if not valid_libs[s] then
                error("Bad library name: " .. string_format("%q", tostring(s)), 2)
            end
            if question == '' and not supported_libs[s] then
                error("Unsupported library: " .. string_format("%q", tostring(s)), 2)
            end
            found[s] = (found[s] or 0) + 1
            if found[s] > 1 then
                error("Libs specification contains " .. string_format("%q", tostring(s)) .. " more than once", 2)
            end
        end
    end

    -- validate that each option is known and properly valued
    for k, v in pairs(opt) do
        local validator = opt_validators[k]
        if not validator then
            error((type(k) == "number" and "Unkeyed option: " .. type(v) .. " " .. string_format("%q", tostring(v)) or "Bad '" .. tostring(k) .. "' option"), 2)
        else
            opt[k] = validator(v)
        end
    end
    return func, libs, opt
end -- process_gen_opt

-- lane_h[1..n]: lane results, same as via 'lane_h:join()'
-- lane_h[0]:    can be read to make sure a thread has finished (always gives 'true')
-- lane_h[-1]:   error message, without propagating the error
--
--      Reading a Lane result (or [0]) propagates a possible error in the lane
--      (and execution does not return). Cancelled lanes give 'nil' values.
--
-- lane_h.state: "pending"/"running"/"waiting"/"done"/"error"/"cancelled"
--
-- Note: Would be great to be able to have '__ipairs' metamethod, that gets
--      called by 'ipairs()' function to custom iterate objects. We'd use it
--      for making sure a lane has ended (results are available); not requiring
--      the user to precede a loop by explicit 'h[0]' or 'h:join()'.
--
--      Or, even better, 'ipairs()' should start valuing '__index' instead
--      of using raw reads that bypass it.
--
-----
-- lanes.gen([libs_str|opt_tbl [, ...],] lane_func ) ([...]) -> h
--
-- 'libs': nil:     no libraries available (default)
--         "":      only base library ('assert', 'print', 'unpack' etc.)
--         "math,os": math + os + base libraries (named ones + base)
--         "*":     all standard libraries available
--
-- 'opt': .priority:  int (-3..+3) smaller is lower priority (0 = default)
--
--        .globals:  table of globals to set for a new thread (passed by value)
--
--        .required: table of packages to require
--
--        .gc_cb:    function called when the lane handle is collected
--
--        ... (more options may be introduced later) ...
--
-- Calling with a function argument ('lane_func') ends the string/table
-- modifiers, and prepares a lane generator.

-- receives a sequence of strings and tables, plus a function
local gen = function(...)
    local func, libs, opt = process_gen_opt(...)
    local core_lane_new = assert(core.lane_new)
    local priority, globals, package, required, gc_cb, name, error_trace_level = opt.priority, opt.globals, opt.package or package, opt.required, opt.gc_cb, opt.name, error_trace_levels[opt.error_trace_level]
    return function(...)
        -- must pass functions args last else they will be truncated to the first one
        return core_lane_new(func, libs, priority, globals, package, required, gc_cb, name, error_trace_level, false, ...)
    end
end -- gen()

local coro = function(...)
    local func, libs, opt = process_gen_opt(...)
    local core_lane_new = assert(core.lane_new)
    local priority, globals, package, required, gc_cb, name, error_trace_level = opt.priority, opt.globals, opt.package or package, opt.required, opt.gc_cb, opt.name, error_trace_levels[opt.error_trace_level]
    return function(...)
        -- must pass functions args last else they will be truncated to the first one
        return core_lane_new(func, libs, priority, globals, package, required, gc_cb, name, error_trace_level, true, ...)
    end
end -- coro()

-- #################################################################################################
-- ####################################### Timers ##################################################
-- #################################################################################################

-- PUBLIC LANES API
local timer = function() error "timers are not active" end
local timers = timer
local timer_lane = nil

-- timerLinda should always exist, even when the settings disable the timers
-- is upvalue of timer stuff and lanes.sleep()
local timerLinda

local TGW_KEY = "(timer control)"    -- the key does not matter, a 'weird' key may help debugging
local TGW_QUERY, TGW_REPLY = "(timer query)", "(timer reply)"

-- #################################################################################################

local configure_timers = function()
    -- On first 'require "lanes"', a timer lane is spawned that will maintain
    -- timer tables and sleep in between the timer events. All interaction with
    -- the timer lane happens via a 'timerLinda' Linda, which is common to
    -- all that 'require "lanes"'.
    --
    -- Linda protocol to timer lane:
    --
    --  TGW_KEY: linda_h, key, [wakeup_at_secs], [repeat_secs]

    local now_secs = core.now_secs
    local wakeup_conv = core.wakeup_conv

    -- Timer lane; initialize only on the first 'require "lanes"' instance (which naturally has 'table' always declared)
    local first_time_key = "first time"
    local _, _first_time_val = timerLinda:get(first_time_key)
    local first_time = (_first_time_val == nil)
    timerLinda:set(first_time_key, true)
    if first_time then

        assert(type(now_secs) == "function")
        -----
        -- Snore loop (run as a lane on the background)
        --
        -- High priority, to get trustworthy timings.
        --
        -- We let the timer lane be a "free running" thread; no handle to it
        -- remains.
        --
        local timer_body = function()
            --
            -- { [deep_linda_lightuserdata]= { [deep_linda_lightuserdata]=linda_h,
            --                                 [key]= { wakeup_secs [,period_secs] } [, ...] },
            -- }
            --
            -- Collection of all running timers, indexed with linda's & key.
            --
            -- Note that we need to use the deep lightuserdata identifiers, instead
            -- of 'linda_h' themselves as table indices. Otherwise, we'd get multiple
            -- entries for the same timer.
            --
            -- The 'hidden' reference to Linda proxy is used in 'check_timers()' but
            -- also important to keep the Linda alive, even if all outside world threw
            -- away pointers to it (which would ruin uniqueness of the deep pointer).
            -- Now we're safe.
            --
            local collection = {}

            local get_timers = function()
                local r = {}
                for deep, t in pairs(collection) do
                    -- WR(tostring(deep))
                    local l = t[deep]
                    for key, timer_data in pairs(t) do
                        if key ~= deep then
                            table_insert(r, {l, key, timer_data})
                        end
                    end
                end
                return r
            end -- get_timers()

            --
            -- set_timer(linda_h, key [,wakeup_at_secs [,period_secs]] )
            --
            local set_timer = function(linda_, key_, wakeup_at_, period_)
                assert(wakeup_at_ == nil or wakeup_at_ > 0.0)
                assert(period_ == nil or period_ > 0.0)

                local linda_deep = linda_:deep()
                assert(linda_deep)

                -- Find or make a lookup for this timer
                --
                local t1 = collection[linda_deep]
                if not t1 then
                    t1 = { [linda_deep] = linda_}     -- proxy to use the Linda
                    collection[linda_deep] = t1
                end

                if wakeup_at_ == nil then
                    -- Clear the timer
                    --
                    t1[key_]= nil

                    -- Remove empty tables from collection; speeds timer checks and
                    -- lets our 'safety reference' proxy be gc:ed as well.
                    --
                    local empty = true
                    for k, _ in pairs(t1) do
                        if k ~= linda_deep then
                            empty = false
                            break
                        end
                    end
                    if empty then
                        collection[linda_deep] = nil
                    end

                    -- Note: any unread timer value is left at 'linda_[key_]' intensionally;
                    --       clearing a timer just stops it.
                else
                    -- New timer or changing the timings
                    --
                    local t2 = t1[key_]
                    if not t2 then
                        t2 = {}
                        t1[key_] = t2
                    end

                    t2[1] = wakeup_at_
                    t2[2] = period_   -- can be 'nil'
                end
            end -- set_timer()

            -----
            -- [next_wakeup_at]= check_timers()
            -- Check timers, and wake up the ones expired (if any)
            -- Returns the closest upcoming (remaining) wakeup time (or 'nil' if none).
            local check_timers = function()
                local now = now_secs()
                local next_wakeup

                for linda_deep, t1 in pairs(collection) do
                    for key, t2 in pairs(t1) do
                        --
                        if key == linda_deep then
                            -- no 'continue' in Lua :/
                        else
                            -- 't2': { wakeup_at_secs [,period_secs] }
                            --
                            local wakeup_at= t2[1]
                            local period= t2[2]     -- may be 'nil'

                            if wakeup_at <= now then
                                local linda = t1[linda_deep]
                                assert(linda)

                                linda:set(key, now)

                                -- 'pairs()' allows the values to be modified (and even
                                -- removed) as far as keys are not touched

                                if not period then
                                    -- one-time timer; gone
                                    --
                                    t1[key] = nil
                                    wakeup_at = nil   -- no 'continue' in Lua :/
                                else
                                    -- repeating timer; find next wakeup (may jump multiple repeats)
                                    --
                                    repeat
                                        wakeup_at= wakeup_at+period
                                    until wakeup_at > now

                                    t2[1]= wakeup_at
                                end
                            end

                            if wakeup_at and ((not next_wakeup) or (wakeup_at < next_wakeup)) then
                                next_wakeup = wakeup_at
                            end
                        end
                    end -- t2 loop
                end -- t1 loop

                return next_wakeup  -- may be 'nil'
            end -- check_timers()

            local timer_gateway_batched = timerLinda.batched
            set_finalizer(function(err, stk)
                if err and type(err) ~= "userdata" then
                    error("LanesTimer error: "..tostring(err))
                --elseif type(err) == "userdata" then
                --	WR("LanesTimer after cancel" )
                --else
                --	WR("LanesTimer finalized")
                end
            end)
            while true do
                local next_wakeup = check_timers()

                -- Sleep until next timer to wake up, or a set/clear command
                --
                local secs
                if next_wakeup then
                    secs =  next_wakeup - now_secs()
                    if secs < 0 then secs = 0 end
                end
                -- poll both TGW_KEY and TGW_QUERY at the same time
                local _timerKey, _what = timerLinda:receive(secs, TGW_KEY, TGW_QUERY)

                if _timerKey == TGW_KEY then
                    assert(getmetatable(_what) == "Linda") -- '_what' should be a linda on which the client sets a timer
                    local _, key, wakeup_at, period = timerLinda:receive(0, timer_gateway_batched, TGW_KEY, 3)
                    assert(key)
                    set_timer(_what, key, wakeup_at, period and period > 0 and period or nil)
                elseif _timerKey == TGW_QUERY then
                    if _what == "get_timers" then
                        timerLinda:send(TGW_REPLY, get_timers())
                    else
                        timerLinda:send(TGW_REPLY, "unknown query " .. _what)
                    end
                else -- got no value while block-waiting
                    assert(_what == cancel_error or _what == "timeout")
                --	WR("timer lane: no linda, aborted?")
                end
            end
        end -- timer_body()
        timer_lane = gen("lanes.core,table", { name = "LanesTimer", package = {}, priority = core.max_prio }, timer_body)()
    end -- first_time

    -----
    -- = timer(linda_h, key_val, date_tbl|first_secs [,period_secs] )
    --
    -- PUBLIC LANES API
    timer = function(linda_, key_, when_, period_)
        if getmetatable(linda_) ~= "Linda" then
            error "expecting a Linda"
        end
        if when_ == 0.0 then
            -- Caller expects to get current time stamp in Linda, on return
            -- (like the timer had expired instantly); it would be good to set this
            -- as late as possible (to give most current time) but also we want it
            -- to precede any possible timers that might start striking.
            --
            linda_:set(key_, now_secs())

            if not period_ or period_ == 0.0 then
                timerLinda:send(TGW_KEY, linda_, key_, nil, nil )   -- clear the timer
                return  -- nothing more to do
            end
            when_ = period_
        end

        local wakeup_at = type(when_)=="table" and wakeup_conv(when_)    -- given point of time
                                            or (when_ and now_secs()+when_ or nil)
        -- queue to timer
        --
        timerLinda:send(TGW_KEY, linda_, key_, wakeup_at, period_)
    end -- timer()

    -----
    -- {[{linda, slot, when, period}[,...]]} = timers()
    --
    -- PUBLIC LANES API
    timers = function()
        timerLinda:send(TGW_QUERY, "get_timers")
        -- can be nil, <something> in case of cancellation or timeout
        local _k, _t = timerLinda:receive(TGW_REPLY)
        -- success: return the table
        if _k then
            return _t
        end
        -- error: return everything we got
        return _k, _t
    end -- timers()
end -- configure_timers()

-- #################################################################################################
-- ##################################### lanes.genlock() ###########################################
-- #################################################################################################
    -- These functions are just surface sugar, but make solutions easier to read.
    -- Not many applications should even need explicit locks or atomic counters.

--
-- [true [, ...]= trues(uint)
--
local function trues(n)
    if n > 0 then
        return true, trues(n - 1)
    end
end

-- avoid pulling the whole core module as upvalue when cancel_error is enough
local cancel_error

-- #################################################################################################

-- lock_f = lanes.genlock(linda_h, key [,N_uint=1] )
--
-- = lock_f(+M)   -- acquire M
--      ...locked...
-- = lock_f(-M)   -- release M
--
-- Returns an access function that allows 'N' simultaneous entries between
-- acquire (+M) and release (-M). For binary locks, use M==1.
--
local genlock = function(linda_, key_, N)
    -- clear existing data and set the limit
    N = N or 1
    local _status, _err = linda_:set(key_)
    if _err == cancel_error then
        return cancel_error
    end
    local _status, _err = linda_:limit(key_, N)
    if _err == cancel_error then
        return cancel_error
    end

    -- use an optimized version for case N == 1
    return (N == 1) and
    function(M_, mode_)
        local timeout = (mode_ == "try") and 0 or nil
        if M_ > 0 then
            -- 'nil' timeout allows 'key_' to be numeric
            local _status, _err = linda_:send(timeout, key_, true) -- suspends until been able to push them
            -- if success, _status is true, that's what we return
            -- if failure, _status is nil, _err contains the error, that's what we return
            -- propagate cancel_error if we got it, else return true or false
            return (_err == cancel_error and _err) or (_status and true or false)
        else
            local _k, _v = linda_:receive(nil, key_)
            -- propagate cancel_error if we got it, else return true or false
            return (_v == cancel_error and _v) or (_k and true or false)
        end
    end
    or
    function(M_, mode_)
        local timeout = (mode_ == "try") and 0 or nil
        if M_ > 0 then
            -- 'nil' timeout allows 'key_' to be numeric
            return linda_:send(timeout, key_, trues(M_))    -- suspends until been able to push them
        else
            local _k, _v = linda_:receive(nil, linda_.batched, key_, -M_)
            -- propagate cancel_error if we got it, else return true or false
            return (_v == cancel_error and _v) or (_k and true or false)
        end
    end
end -- genlock

-- #################################################################################################
-- #################################### lanes.genatomic() ##########################################
-- #################################################################################################

-- atomic_f = lanes.genatomic(linda_h, key [,initial_num=0.0])
--
-- int|cancel_error = atomic_f([diff_num = 1.0])
--
-- Returns an access function that allows atomic increment/decrement of the
-- number in 'key'.
--
local genatomic = function(linda_, key_, initial_val_)
    -- clears existing data (also queue). the slot may contain the stored value, and an additional boolean value
    local _status, _err = linda_:limit(key_, 2)
    if _err == cancel_error then
        return cancel_error
    end
    local _status, _err = linda_:set(key_, initial_val_ or 0.0)
    if _err == cancel_error then
        return cancel_error
    end

    return function(diff_)
        -- 'nil' allows 'key_' to be numeric
        -- suspends until our 'true' is in
        local _res, _err = linda_:send(nil, key_, true)
        if _err == cancel_error then
            return cancel_error
        end
        local _, val = linda_:get(key_)
        if val ~= cancel_error then
            val = val + (diff_ or 1.0)
            -- set() releases the lock by emptying queue
            local _res, _err = linda_:set(key_, val)
            if _err == cancel_error then
                val = cancel_error
            end
        end
        return val
    end
end -- genatomic

-- #################################################################################################
-- ################################## lanes.configure() ############################################
-- #################################################################################################

-- start with a protected metatable
local lanesMeta = { __metatable = "Lanes" }

-- this function is available in the public interface until it is called, after which it disappears
local configure = function(settings_)
    -- Configure called so remove metatable from lanes
    lanesMeta.__metatable = nil -- unprotect the metatable
    setmetatable(lanes, nil) -- remove it
    lanes.configure = function() return lanes end -- no need to configure anything again

    -- now we can configure Lanes core





    local settings = core.configure and core.configure(params_checker(settings_)) or core.settings

    --
    lanes.ABOUT =
    {
        author= "Asko Kauppi <akauppi@gmail.com>, Benoit Germain <bnt.germain@gmail.com>",
        description= "Running multiple Lua states in parallel",
        license= "MIT/X11",
        copyright= "Copyright (c) 2007-10, Asko Kauppi; (c) 2011-23, Benoit Germain",
        version = assert(core.version)
    }

    -- avoid pulling the whole core module as upvalue when cancel_error is enough
    -- these are locals declared above, that we need to set prior to calling configure_timers()
    cancel_error = assert(core.cancel_error)
    supported_libs = assert(core.supported_libs())
    timerLinda = assert(core.timerLinda)

    if settings.with_timers then
        configure_timers(settings)
    end

    -- activate full interface
    lanes.cancel_error = core.cancel_error
    lanes.finally = core.finally
    lanes.linda = core.linda
    lanes.nameof = core.nameof
    lanes.now_secs = core.now_secs
    lanes.null = core.null
    lanes.register = core.register
    lanes.require = core.require
    lanes.set_singlethreaded = core.set_singlethreaded
    lanes.set_thread_affinity = core.set_thread_affinity
    lanes.set_thread_priority = core.set_thread_priority
    lanes.sleep = core.sleep
    lanes.threads = core.threads or function() error "lane tracking is not available" end -- core.threads isn't registered if settings.track_lanes is false

    lanes.gen = gen
    lanes.coro = coro
    lanes.genatomic = genatomic
    lanes.genlock = genlock
    lanes.timer = timer
    lanes.timer_lane = timer_lane
    lanes.timers = timers
    return lanes
end -- lanes.configure

-- #################################################################################################

lanesMeta.__index = function(lanes_, k_)
    -- This is called when some functionality is accessed without calling configure()
    configure() -- initialize with default settings
    -- Access the required key
    return lanes_[k_]
end
lanes.configure = configure
setmetatable(lanes, lanesMeta)

-- #################################################################################################

-- no need to force calling configure() manually excepted the first time (other times will reuse the internally stored settings of the first call)
if core.settings then
    return configure()
else
    return lanes
end

--the end
