--
-- LANES.LUA
--
-- Multithreading and -core support for Lua
--
-- Author: Asko Kauppi <akauppi@gmail.com>
--
-- History:
--    3-Dec-10  BGe: Added support to generate a lane from a string
--    Jun-08    AKa: major revise
--    15-May-07 AKa: pthread_join():less version, some speedup & ability to
--                   handle more threads (~ 8000-9000, up from ~ 5000)
--    26-Feb-07 AKa: serialization working (C side)
--    17-Sep-06 AKa: started the module (serialization)
--
--[[
===============================================================================

Copyright (C) 2007-10 Asko Kauppi <akauppi@gmail.com>

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

-- Lua 5.1: module() creates a global variable
-- Lua 5.2: module() might go away
-- almost everything module() does is done by require()
-- -> simply create a table, populate it, return it, and be done
local lanes = {}

lanes.configure = function( _params)
_params = _params or { nb_keepers = 1, with_timers = true, on_state_create = nil}
if type( _params) ~= "table" then
	error( "Bad parameter #1 to lanes.configure(), should be a table")
end
-- on_state_create may be nil or a function
if _params.on_state_create and (type( _params.on_state_create) ~= "function") then
	error( "Bad on_state_create: " .. tostring( _params.on_state_create), 2)
end

local mm = require "lanes.core"
assert( type(mm)=="table" )

-- configure() is available only the first time lanes.core is required process-wide, and we *must* call it to have the other functions in the interface
if mm.configure then mm.configure( _params.nb_keepers, _params.on_state_create) end

local thread_new = assert(mm.thread_new)

local _single= assert(mm._single)
local _version= assert(mm._version)

local now_secs= assert( mm.now_secs )
local wakeup_conv= assert( mm.wakeup_conv )

local max_prio= assert( mm.max_prio )

-- This check is for sublanes requiring Lanes
--
-- TBD: We could also have the C level expose 'string.gmatch' for us. But this is simpler.
--
if not string then
    error( "To use 'lanes', you will also need to have 'string' available.", 2 )
end

-- 
-- Cache globals for code that might run under sandboxing 
--
local assert= assert
local string_gmatch= assert( string.gmatch )
local select= assert( select )
local type= assert( type )
local pairs= assert( pairs )
local tostring= assert( tostring )
local error= assert( error )

lanes.ABOUT= 
{
    author= "Asko Kauppi <akauppi@gmail.com>, Benoit Germain <bnt.germain@gmail.com>",
    description= "Running multiple Lua states in parallel",
    license= "MIT/X11",
    copyright= "Copyright (c) 2007-10, Asko Kauppi; (c) 2011-12, Benoit Germain",
    version= _version,
}


-- Making copies of necessary system libs will pass them on as upvalues;
-- only the first state doing "require 'lanes'" will need to have 'string'
-- and 'table' visible.
--
local function WR(str)
    io.stderr:write( str.."\n" )
end

local function DUMP( tbl )
    if not tbl then return end
    local str=""
    for k,v in pairs(tbl) do
        str= str..k.."="..tostring(v).."\n"
    end
    WR(str)
end


---=== Laning ===---

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
-- lanes.gen( [libs_str|opt_tbl [, ...],] lane_func ) ( [...] ) -> h
--
-- 'libs': nil:     no libraries available (default)
--         "":      only base library ('assert', 'print', 'unpack' etc.)
--         "math,os": math + os + base libraries (named ones + base)
--         "*":     all standard libraries available
--
-- 'opt': .priority:  int (-2..+2) smaller is lower priority (0 = default)
--
--	      .cancelstep: bool | uint
--            false: cancellation check only at pending Linda operations
--                   (send/receive) so no runtime performance penalty (default)
--            true:  adequate cancellation check (same as 100)
--            >0:    cancellation check every x Lua lines (small number= faster
--                   reaction but more performance overhead)
--
--        .globals:  table of globals to set for a new thread (passed by value)
--
--        .required:  table of packages to require
--        ... (more options may be introduced later) ...
--
-- Calling with a function parameter ('lane_func') ends the string/table
-- modifiers, and prepares a lane generator. One can either finish here,
-- and call the generator later (maybe multiple times, with different parameters) 
-- or add on actual thread arguments to also ignite the thread on the same call.

local valid_libs= {
    ["package"]= true,
    ["table"]= true,
    ["io"]= true,
    ["os"]= true,
    ["string"]= true,
    ["math"]= true,
    ["debug"]= true,
    --
    ["base"]= true,
    ["coroutine"]= true,
    ["*"]= true
}

-- PUBLIC LANES API
local function gen( ... )
    local opt= {}
    local libs= nil
    local lev= 2  -- level for errors

    local n= select('#',...)
    
    if n==0 then
        error( "No parameters!" )
    end

    for i=1,n-1 do
        local v= select(i,...)
        if type(v)=="string" then
            libs= libs and libs..","..v or v
        elseif type(v)=="table" then
            for k,vv in pairs(v) do
                opt[k]= vv
            end
        elseif v==nil then
            -- skip
        else
            error( "Bad parameter: "..tostring(v) )
        end
    end

    local func= select(n,...)
    local functype = type(func)
    if functype ~= "function" and functype ~= "string" then
        error( "Last parameter not function or string: "..tostring(func))
    end

    -- Check 'libs' already here, so the error goes in the right place
    -- (otherwise will be noticed only once the generator is called)
    --
    if libs then
        for s in string_gmatch(libs, "[%a*]+") do
            if not valid_libs[s] then
                error( "Bad library name: "..s )
            end
        end
    end
    
    local prio, cs, g_tbl, package_tbl, required

    for k,v in pairs(opt) do
            if k=="priority" then prio= v
        elseif k=="cancelstep" then
            cs = (v==true) and 100 or
                (v==false) and 0 or 
                type(v)=="number" and v or
                error( "Bad cancelstep: "..tostring(v), lev )
        elseif k=="globals" then g_tbl= v
        elseif k=="package" then
            package_tbl = (type( v) == "table") and v or error( "Bad package: " .. tostring( v), lev)
        elseif k=="required" then
            required= (type( v) == "table") and v or error( "Bad required: " .. tostring( v), lev)
        --..
        elseif k==1 then error( "unkeyed option: ".. tostring(v), lev )
        else error( "Bad option: ".. tostring(k), lev )
        end
    end

    if not package_tbl then package_tbl = package end
    -- Lane generator
    --
    return function(...)
              return thread_new( func, libs, _params.on_state_create, cs, prio, g_tbl, package_tbl, required, ...)     -- args
           end
end

---=== Lindas ===---

-- We let the C code attach methods to userdata directly

-----
-- lanes.linda(["name"]) -> linda_ud
--
-- PUBLIC LANES API
local linda = mm.linda


---=== Timers ===---

-- PUBLIC LANES API
local timer = function() error "timers are not active" end

if _params.with_timers ~= false then

local timer_gateway= assert( mm.timer_gateway )
--
-- On first 'require "lanes"', a timer lane is spawned that will maintain
-- timer tables and sleep in between the timer events. All interaction with
-- the timer lane happens via a 'timer_gateway' Linda, which is common to
-- all that 'require "lanes"'.
-- 
-- Linda protocol to timer lane:
--
--  TGW_KEY: linda_h, key, [wakeup_at_secs], [repeat_secs]
--
local TGW_KEY= "(timer control)"    -- the key does not matter, a 'weird' key may help debugging
local first_time_key= "first time"

local first_time= timer_gateway:get(first_time_key) == nil
timer_gateway:set(first_time_key,true)

--
-- Timer lane; initialize only on the first 'require "lanes"' instance (which naturally
-- has 'table' always declared)
--
if first_time then
    local table_remove= assert( table.remove )
    local table_insert= assert( table.insert )

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
    local collection= {}

    --
    -- set_timer( linda_h, key [,wakeup_at_secs [,period_secs]] )
    --
    local function set_timer( linda, key, wakeup_at, period )
        assert( wakeup_at==nil or wakeup_at>0.0 )
        assert( period==nil or period>0.0 )

        local linda_deep= linda:deep()
        assert( linda_deep )

        -- Find or make a lookup for this timer
        --
        local t1= collection[linda_deep]
        if not t1 then
            t1= { [linda_deep]= linda }     -- proxy to use the Linda
            collection[linda_deep]= t1
        end
    
        if wakeup_at==nil then
            -- Clear the timer
            --
            t1[key]= nil

            -- Remove empty tables from collection; speeds timer checks and
            -- lets our 'safety reference' proxy be gc:ed as well.
            --
            local empty= true
            for k,_ in pairs(t1) do
                if k~= linda_deep then
                    empty= false; break
                end
            end
            if empty then
                collection[linda_deep]= nil
            end
            
            -- Note: any unread timer value is left at 'linda[key]' intensionally;
            --       clearing a timer just stops it.
        else
            -- New timer or changing the timings
            --
            local t2= t1[key]
            if not t2 then
                t2= {}; t1[key]= t2
            end
    
            t2[1]= wakeup_at
            t2[2]= period   -- can be 'nil'
        end
    end

    -----
    -- [next_wakeup_at]= check_timers()
    --
    -- Check timers, and wake up the ones expired (if any)
    --
    -- Returns the closest upcoming (remaining) wakeup time (or 'nil' if none).
    --
    local function check_timers()

        local now= now_secs()
        local next_wakeup

        for linda_deep,t1 in pairs(collection) do
            for key,t2 in pairs(t1) do
                --
                if key==linda_deep then
                    -- no 'continue' in Lua :/
                else
                    -- 't2': { wakeup_at_secs [,period_secs] }
                    --
                    local wakeup_at= t2[1]
                    local period= t2[2]     -- may be 'nil'
                    
                    if wakeup_at <= now then    
                        local linda= t1[linda_deep]
                        assert(linda)
    
                        linda:set( key, now )
        
                        -- 'pairs()' allows the values to be modified (and even
                        -- removed) as far as keys are not touched
                                                
                        if not period then
                            -- one-time timer; gone
                            --
                            t1[key]= nil
                            wakeup_at= nil   -- no 'continue' in Lua :/
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
                        next_wakeup= wakeup_at
                    end 
                end
            end -- t2 loop
        end -- t1 loop
        
        return next_wakeup  -- may be 'nil'
    end    

    -----
    -- Snore loop (run as a lane on the background)
    --
    -- High priority, to get trustworthy timings.
    --
    -- We let the timer lane be a "free running" thread; no handle to it
    -- remains.
    --
	local timer_body = function()
		local timer_gateway_batched = timer_gateway.batched
		set_debug_threadname( "LanesTimer")
		set_finalizer( function( err, stk)
			if err and type( err) ~= "userdata" then
				WR( "LanesTimer error: "..tostring(err))
			--elseif type( err) == "userdata" then
			--	WR( "LanesTimer after cancel" )
			--else
			--	WR("LanesTimer finalized")
			end
		end)
		while true do
			local next_wakeup= check_timers()

			-- Sleep until next timer to wake up, or a set/clear command
			--
			local secs
			if next_wakeup then
				secs =  next_wakeup - now_secs()
				if secs < 0 then secs = 0 end
			end
			local linda = timer_gateway:receive( secs, TGW_KEY)

			if linda then
				local key, wakeup_at, period = timer_gateway:receive( 0, timer_gateway_batched, TGW_KEY, 3)
				assert( key)
				set_timer( linda, key, wakeup_at, period and period > 0 and period or nil)
			--elseif secs == nil then -- got no value while block-waiting?
			--	WR( "timer lane: no linda, aborted?")
			end
		end
	end
	gen( "*", { priority=max_prio}, timer_body)() -- "*" instead of "io,package" for LuaJIT compatibility...
end

-----
-- = timer( linda_h, key_val, date_tbl|first_secs [,period_secs] )
--
-- PUBLIC LANES API
timer = function( linda, key, a, period )

    if a==0.0 then
        -- Caller expects to get current time stamp in Linda, on return
        -- (like the timer had expired instantly); it would be good to set this
        -- as late as possible (to give most current time) but also we want it
        -- to precede any possible timers that might start striking.
        --
        linda:set( key, now_secs() )

        if not period or period==0.0 then
            timer_gateway:send( TGW_KEY, linda, key, nil, nil )   -- clear the timer
            return  -- nothing more to do
        end
        a= period
    end

    local wakeup_at= type(a)=="table" and wakeup_conv(a)    -- given point of time
                                       or (a and now_secs()+a or nil)
    -- queue to timer
    --
    timer_gateway:send( TGW_KEY, linda, key, wakeup_at, period )
end

end -- _params.with_timers

---=== Lock & atomic generators ===---

-- These functions are just surface sugar, but make solutions easier to read.
-- Not many applications should even need explicit locks or atomic counters.

--
-- lock_f= lanes.genlock( linda_h, key [,N_uint=1] )
--
-- = lock_f( +M )   -- acquire M
--      ...locked...
-- = lock_f( -M )   -- release M
--
-- Returns an access function that allows 'N' simultaneous entries between
-- acquire (+M) and release (-M). For binary locks, use M==1.
--
-- PUBLIC LANES API
local function genlock( linda, key, N )
    linda:limit(key,N)
    linda:set(key,nil)  -- clears existing data

    --
    -- [true [, ...]= trues(uint)
    --
    local function trues(n)
        if n>0 then return true,trues(n-1) end
    end

    return
    function(M)
        if M>0 then
            -- 'nil' timeout allows 'key' to be numeric
            linda:send( nil, key, trues(M) )    -- suspends until been able to push them
        else
            for i=1,-M do
                linda:receive( key )
            end
        end
    end
end


--
-- atomic_f= lanes.genatomic( linda_h, key [,initial_num=0.0] )
--
-- int= atomic_f( [diff_num=1.0] )
--
-- Returns an access function that allows atomic increment/decrement of the
-- number in 'key'.
--
-- PUBLIC LANES API
local function genatomic( linda, key, initial_val )
    linda:limit(key,2)          -- value [,true]
    linda:set(key,initial_val or 0.0)   -- clears existing data (also queue)

    return
    function(diff)
        -- 'nil' allows 'key' to be numeric
        linda:send( nil, key, true )    -- suspends until our 'true' is in
        local val= linda:get(key) + (diff or 1.0)
        linda:set( key, val )   -- releases the lock, by emptying queue
        return val
    end
end

-- newuserdata = mm.newuserdata

	-- activate full interface
	lanes.gen = gen
	lanes.linda = mm.linda
	lanes.cancel_error = mm.cancel_error
	lanes.nameof = mm.nameof
	lanes.timer = timer
	lanes.genlock = genlock
	lanes.now_secs = now_secs
	lanes.genatomic = genatomic
	-- from now on, calling configure does nothing but checking that we don't call it with parameters that changed compared to the first invocation
	lanes.configure = function( _params2)
		_params2 = _params2 or _params
		if _params2.nb_keepers ~= _params.nb_keepers then
			error( "mismatched configuration: " .. tostring( _params2.nb_keepers) .. " keepers instead of " .. tostring( _params.nb_keepers))
		end
		if _params2.with_timers ~= _params.with_timers then
			error( "mismatched configuration: " .. tostring( _params2.with_timers) .. " timer activity instead of " .. tostring( _params.with_timers))
		end
		if _params2.on_create_state and _params2.on_create_state ~= _params.on_create_state then
			error( "mismatched configuration: " .. tostring( _params2.on_create_state) .. " timer activity instead of " .. tostring( _params.on_create_state))
		end
		return lanes
	end
	return lanes
end -- lanes.configure

--the end
return lanes

