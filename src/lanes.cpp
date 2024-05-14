/*
 * LANES.CPP                            Copyright (c) 2007-08, Asko Kauppi
 *                                      Copyright (C) 2009-24, Benoit Germain
 *
 * Multithreading in Lua.
 *
 * History:
 *      See CHANGES
 *
 * Platforms (tested internally):
 *      OS X (10.5.7 PowerPC/Intel)
 *      Linux x86 (Ubuntu 8.04)
 *      Win32 (Windows XP Home SP2, Visual C++ 2005/2008 Express)
 *
 * Platforms (tested externally):
 *      Win32 (MSYS) by Ross Berteig.
 *
 * Platforms (testers appreciated):
 *      Win64 - should work???
 *      Linux x64 - should work
 *      FreeBSD - should work
 *      QNX - porting shouldn't be hard
 *      Sun Solaris - porting shouldn't be hard
 *
 * References:
 *      "Porting multithreaded applications from Win32 to Mac OS X":
 *      <http://developer.apple.com/macosx/multithreadedprogramming.html>
 *
 *      Pthreads:
 *      <http://vergil.chemistry.gatech.edu/resources/programming/threads.html>
 *
 *      MSDN: <http://msdn2.microsoft.com/en-us/library/ms686679.aspx>
 *
 *      <http://ridiculousfish.com/blog/archives/2007/02/17/barrier>
 *
 * Defines:
 *      -DLINUX_SCHED_RR: all threads are lifted to SCHED_RR category, to
 *          allow negative priorities [-3,-1] be used. Even without this,
 *          using priorities will require 'sudo' privileges on Linux.
 *
 *      -DUSE_PTHREAD_TIMEDJOIN: use 'pthread_timedjoin_np()' for waiting
 *          for threads with a timeout. This changes the thread cleanup
 *          mechanism slightly (cleans up at the join, not once the thread
 *          has finished). May or may not be a good idea to use it.
 *          Available only in selected operating systems (Linux).
 *
 * Bugs:
 *
 * To-do:
 *
 * Make waiting threads cancellable.
 *      ...
 */

/*
===============================================================================

Copyright (C) 2007-10 Asko Kauppi <akauppi@gmail.com>
              2011-24 Benoit Germain <bnt.germain@gmail.com>

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
*/

#include "lanes.h"

#include "deep.h"
#include "intercopycontext.h"
#include "keeper.h"
#include "lane.h"
#include "state.h"
#include "threading.h"
#include "tools.h"

#if !(defined(PLATFORM_XBOX) || defined(PLATFORM_WIN32) || defined(PLATFORM_POCKETPC))
#include <sys/time.h>
#endif

/* geteuid() */
#ifdef PLATFORM_LINUX
#include <unistd.h>
#include <sys/types.h>
#endif

#include <atomic>

// #################################################################################################
// ########################################### Threads #############################################
// #################################################################################################

//---
// = _single( [cores_uint=1] )
//
// Limits the process to use only 'cores' CPU cores. To be used for performance
// testing on multicore devices. DEBUGGING ONLY!
//
LUAG_FUNC(set_singlethreaded)
{
    [[maybe_unused]] lua_Integer const _cores{ luaL_optinteger(L_, 1, 1) };

#ifdef PLATFORM_OSX
#ifdef _UTILBINDTHREADTOCPU
    if (_cores > 1) {
        raise_luaL_error(L_, "Limiting to N>1 cores not possible");
    }
    // requires 'chudInitialize()'
    utilBindThreadToCPU(0); // # of CPU to run on (we cannot limit to 2..N CPUs?)
    return 0;
#else
    raise_luaL_error(L_, "Not available: compile with _UTILBINDTHREADTOCPU");
#endif
#else
    raise_luaL_error(L_, "not implemented");
#endif
}

// #################################################################################################

LUAG_FUNC(set_thread_priority)
{
    lua_Integer const _prio{ luaL_checkinteger(L_, 1) };
    // public Lanes API accepts a generic range -3/+3
    // that will be remapped into the platform-specific scheduler priority scheme
    // On some platforms, -3 is equivalent to -2 and +3 to +2
    if (_prio < kThreadPrioMin || _prio > kThreadPrioMax) {
        raise_luaL_error(L_, "priority out of range: %d..+%d (%d)", kThreadPrioMin, kThreadPrioMax, _prio);
    }
    THREAD_SET_PRIORITY(static_cast<int>(_prio), universe_get(L_)->sudo);
    return 0;
}

// #################################################################################################

LUAG_FUNC(set_thread_affinity)
{
    lua_Integer const _affinity{ luaL_checkinteger(L_, 1) };
    if (_affinity <= 0) {
        raise_luaL_error(L_, "invalid affinity (%d)", _affinity);
    }
    THREAD_SET_AFFINITY(static_cast<unsigned int>(_affinity));
    return 0;
}

// #################################################################################################

// --- If a client wants to transfer stuff of a given module from the current state to another Lane, the module must be required
// with lanes.require, that will call the regular 'require', then populate the lookup database in the source lane
// module = lanes.require( "modname")
// upvalue[1]: _G.require
LUAG_FUNC(require)
{
    char const* _name{ lua_tostring(L_, 1) };                                                      // L_: "name" ...
    int const _nargs{ lua_gettop(L_) };
    DEBUGSPEW_CODE(Universe * _U{ universe_get(L_) });
    STACK_CHECK_START_REL(L_, 0);
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lanes.require %s BEGIN\n" INDENT_END(_U), _name));
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
    lua_pushvalue(L_, lua_upvalueindex(1));                                                        // L_: "name" ... require
    lua_insert(L_, 1);                                                                             // L_: require "name" ...
    lua_call(L_, _nargs, 1);                                                                       // L_: module
    populate_func_lookup_table(L_, -1, _name);
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lanes.require %s END\n" INDENT_END(_U), _name));
    STACK_CHECK(L_, 0);
    return 1;
}

// #################################################################################################

// --- If a client wants to transfer stuff of a previously required module from the current state to another Lane, the module must be registered
// to populate the lookup database in the source lane (and in the destination too, of course)
// lanes.register( "modname", module)
LUAG_FUNC(register)
{
    char const* _name{ luaL_checkstring(L_, 1) };
    LuaType const _mod_type{ lua_type_as_enum(L_, 2) };
    // ignore extra parameters, just in case
    lua_settop(L_, 2);
    luaL_argcheck(L_, (_mod_type == LuaType::TABLE) || (_mod_type == LuaType::FUNCTION), 2, "unexpected module type");
    DEBUGSPEW_CODE(Universe* U = universe_get(L_));
    STACK_CHECK_START_REL(L_, 0); // "name" mod_table
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lanes.register %s BEGIN\n" INDENT_END(U), _name));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
    populate_func_lookup_table(L_, -1, _name);
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lanes.register %s END\n" INDENT_END(U), _name));
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

//---
// lane_ud = lane_new( function
//                   , [libs_str]
//                   , [priority_int=0]
//                   , [globals_tbl]
//                   , [package_tbl]
//                   , [required_tbl]
//                   , [gc_cb_func]
//                   , [name]
//                  [, ... args ...])
//
// Upvalues: metatable to use for 'lane_ud'
//
LUAG_FUNC(lane_new)
{
    // first 8 args: func libs priority globals package required gc_cb name
    char const* const _libs_str{ lua_tostring(L_, 2) };
    bool const _have_priority{ !lua_isnoneornil(L_, 3) };
    int const _priority{ _have_priority ? static_cast<int>(lua_tointeger(L_, 3)) : kThreadPrioDefault };
    int const _globals_idx{ lua_isnoneornil(L_, 4) ? 0 : 4 };
    int const _package_idx{ lua_isnoneornil(L_, 5) ? 0 : 5 };
    int const _required_idx{ lua_isnoneornil(L_, 6) ? 0 : 6 };
    int const _gc_cb_idx{ lua_isnoneornil(L_, 7) ? 0 : 7 };
    int const _name_idx{ lua_isnoneornil(L_, 8) ? 0 : 8 };

    static constexpr int kFixedArgsIdx{ 8 };
    int const _nargs{ lua_gettop(L_) - kFixedArgsIdx };
    Universe* const _U{ universe_get(L_) };
    LUA_ASSERT(L_, _nargs >= 0);

    // public Lanes API accepts a generic range -3/+3
    // that will be remapped into the platform-specific scheduler priority scheme
    // On some platforms, -3 is equivalent to -2 and +3 to +2
    if (_have_priority && (_priority < kThreadPrioMin || _priority > kThreadPrioMax)) {
        raise_luaL_error(L_, "Priority out of range: %d..+%d (%d)", kThreadPrioMin, kThreadPrioMax, _priority);
    }

    /* --- Create and prepare the sub state --- */
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: setup\n" INDENT_END(_U)));

    // populate with selected libraries at the same time. 
    lua_State* const _L2{ luaG_newstate(_U, SourceState{ L_ }, _libs_str) };                       // L_: [8 args] ...                               L2:
    STACK_CHECK_START_REL(_L2, 0);

    // 'lane' is allocated from heap, not Lua, since its life span may surpass the handle's (if free running thread)
    Lane* const _lane{ new (_U) Lane{ _U, _L2 } };
    if (_lane == nullptr) {
        raise_luaL_error(L_, "could not create lane: out of memory");
    }

    class OnExit
    {
        private:
        lua_State* const L;
        Lane* lane{ nullptr };
        int const gc_cb_idx;
        int const name_idx;
        DEBUGSPEW_CODE(Universe* const U);
        DEBUGSPEW_CODE(DebugSpewIndentScope scope);

        public:
        OnExit(lua_State* L_, Lane* lane_, int gc_cb_idx_, int name_idx_ DEBUGSPEW_COMMA_PARAM(Universe* U_))
        : L{ L_ }
        , lane{ lane_ }
        , gc_cb_idx{ gc_cb_idx_ }
        , name_idx{ name_idx_ }
        DEBUGSPEW_COMMA_PARAM(U{ U_ })
        DEBUGSPEW_COMMA_PARAM(scope{ U_ })
        {
        }

        ~OnExit()
        {
            if (lane) {
                STACK_CHECK_START_REL(L, 0);
                // we still need a full userdata so that garbage collection can do its thing
                prepareUserData();
                // remove it immediately from the stack so that the error that landed us here is at the top
                lua_pop(L, 1);
                STACK_CHECK(L, 0);
                // leave a single cancel_error on the stack for the caller
                lua_settop(lane->L, 0);
                kCancelError.pushKey(lane->L);
                {
                    std::lock_guard _guard{ lane->doneMutex };
                    // this will cause lane_main to skip actual running (because we are not Pending anymore)
                    lane->status = Lane::Running;
                }
                // unblock the thread so that it can terminate gracefully
                lane->ready.count_down();
            }
        }

        private:
        void prepareUserData()
        {
            DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: preparing lane userdata\n" INDENT_END(U)));
            STACK_CHECK_START_REL(L, 0);
            // a Lane full userdata needs a single uservalue
            Lane** const _ud{ lua_newuserdatauv<Lane*>(L, 1) };                                    // L: ... lane
            *_ud = lane; // don't forget to store the pointer in the userdata!

            // Set metatable for the userdata
            //
            lua_pushvalue(L, lua_upvalueindex(1));                                                 // L: ... lane mt
            lua_setmetatable(L, -2);                                                               // L: ... lane
            STACK_CHECK(L, 1);

            // Create uservalue for the userdata
            // (this is where lane body return values will be stored when the handle is indexed by a numeric key)
            lua_newtable(L);                                                                       // L: ... lane {uv}

            // Store the gc_cb callback in the uservalue
            if (gc_cb_idx > 0) {
                kLaneGC.pushKey(L);                                                                // L: ... lane {uv} k
                lua_pushvalue(L, gc_cb_idx);                                                       // L: ... lane {uv} k gc_cb
                lua_rawset(L, -3);                                                                 // L: ... lane {uv}
            }

            lua_setiuservalue(L, -2, 1);                                                           // L: ... lane

            lua_State* _L2{ lane->L };
            STACK_CHECK_START_REL(_L2, 0);
            char const* const debugName{ (name_idx > 0) ? lua_tostring(L, name_idx) : nullptr };
            if (debugName)
            {
                if (strcmp(debugName, "auto") != 0) {
                    lua_pushstring(_L2, debugName);                                                // L: ... lane                                       L2: "<name>"
                } else {
                    lua_Debug ar;
                    lua_pushvalue(L, 1);                                                           // L: ... lane func
                    lua_getinfo(L, ">S", &ar);                                                     // L: ... lane
                    lua_pushfstring(_L2, "%s:%d", ar.short_src, ar.linedefined);                   // L: ... lane                                       L2: "<name>"
                }
                lane->changeDebugName(-1);
                lua_pop(_L2, 1);                                                                   // L: ... lane                                       L2:
            }
            STACK_CHECK(_L2, 0);
            STACK_CHECK(L, 1);
        }

        public:
        void success()
        {
            prepareUserData();
            lane->ready.count_down();
            lane = nullptr;
        }
    } onExit{ L_, _lane, _gc_cb_idx, _name_idx DEBUGSPEW_COMMA_PARAM(_U) };
    // launch the thread early, it will sync with a std::latch to parallelize OS thread warmup and L2 preparation
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: launching thread\n" INDENT_END(_U)));
    _lane->startThread(_priority);

    STACK_GROW(_L2, _nargs + 3);
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);

    // package
    if (_package_idx != 0) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: update 'package'\n" INDENT_END(_U)));
        // when copying with mode LookupMode::LaneBody, should raise an error in case of problem, not leave it one the stack
        InterCopyContext c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, SourceIndex{ _package_idx }, {}, {}, {} };
        [[maybe_unused]] InterCopyResult const ret{ c.inter_copy_package() };
        LUA_ASSERT(L_, ret == InterCopyResult::Success); // either all went well, or we should not even get here
    }

    // modules to require in the target lane *before* the function is transfered!
    if (_required_idx != 0) {
        int _nbRequired{ 1 };
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: require 'required' list\n" INDENT_END(_U)));
        DEBUGSPEW_CODE(DebugSpewIndentScope scope{ _U });
        // should not happen, was checked in lanes.lua before calling lane_new()
        if (lua_type(L_, _required_idx) != LUA_TTABLE) {
            raise_luaL_error(L_, "expected required module list as a table, got %s", luaL_typename(L_, _required_idx));
        }

        lua_pushnil(L_);                                                                           // L_: [8 args] args... nil                       L2:
        while (lua_next(L_, _required_idx) != 0) {                                                 // L_: [8 args] args... n "modname"               L2:
            if (lua_type(L_, -1) != LUA_TSTRING || lua_type(L_, -2) != LUA_TNUMBER || lua_tonumber(L_, -2) != _nbRequired) {
                raise_luaL_error(L_, "required module list should be a list of strings");
            } else {
                // require the module in the target state, and populate the lookup table there too
                size_t len;
                char const* name = lua_tolstring(L_, -1, &len);
                DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: require '%s'\n" INDENT_END(_U), name));

                // require the module in the target lane
                lua_getglobal(_L2, "require");                                                     // L_: [8 args] args... n "modname"               L2: require()?
                if (lua_isnil(_L2, -1)) {
                    lua_pop(_L2, 1);                                                               // L_: [8 args] args... n "modname"               L2:
                    raise_luaL_error(L_, "cannot pre-require modules without loading 'package' library first");
                } else {
                    lua_pushlstring(_L2, name, len);                                               // L_: [8 args] args... n "modname"               L2: require() name
                    if (lua_pcall(_L2, 1, 1, 0) != LUA_OK) {                                       // L_: [8 args] args... n "modname"               L2: ret/errcode
                        // propagate error to main state if any
                        InterCopyContext _c{ _U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} };
                        std::ignore = _c.inter_move(1);                                            // L_: [8 args] args... n "modname" error         L2:
                        raise_lua_error(L_);
                    }
                    // here the module was successfully required                                   // L_: [8 args] args... n "modname"               L2: ret
                    // after requiring the module, register the functions it exported in our name<->function database
                    populate_func_lookup_table(_L2, -1, name);
                    lua_pop(_L2, 1);                                                               // L_: [8 args] args... n "modname"               L2:
                }
            }
            lua_pop(L_, 1); // L_: func libs priority globals package required gc_cb [... args ...] n
            ++_nbRequired;
        }                                                                                          // L_: [8 args] args...
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 0);                                                                           // L_: [8 args] args...                           L2:

    // Appending the specified globals to the global environment
    // *after* stdlibs have been loaded and modules required, in case we transfer references to native functions they exposed...
    //
    if (_globals_idx != 0) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: transfer globals\n" INDENT_END(_U)));
        if (!lua_istable(L_, _globals_idx)) {
            raise_luaL_error(L_, "Expected table, got %s", luaL_typename(L_, _globals_idx));
        }

        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        lua_pushnil(L_);                                                                           // L_: [8 args] args... nil                       L2:
        // Lua 5.2 wants us to push the globals table on the stack
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        lua_pushglobaltable(_L2);                                                                  // L_: [8 args] args... nil                       L2: _G
        while (lua_next(L_, _globals_idx)) {                                                       // L_: [8 args] args... k v                       L2: _G
            std::ignore = _c.inter_copy(2);                                                        // L_: [8 args] args... k v                       L2: _G k v
            // assign it in L2's globals table
            lua_rawset(_L2, -3);                                                                   // L_: [8 args] args... k v                       L2: _G
            lua_pop(L_, 1);                                                                        // L_: [8 args] args... k
        }                                                                                          // L_: [8 args] args...
        lua_pop(_L2, 1);                                                                           // L_: [8 args] args...                           L2:
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 0);

    // Lane main function
    LuaType const _func_type{ lua_type_as_enum(L_, 1) };
    if (_func_type == LuaType::FUNCTION) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: transfer lane body\n" INDENT_END(_U)));
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        lua_pushvalue(L_, 1);                                                                      // L_: [8 args] args... func                      L2:
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        InterCopyResult const _res{ _c.inter_move(1) };                                             // L_: [8 args] args...                           L2: func
        if (_res != InterCopyResult::Success) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }
    } else if (_func_type == LuaType::STRING) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: compile lane body\n" INDENT_END(_U)));
        // compile the string
        if (luaL_loadstring(_L2, lua_tostring(L_, 1)) != 0) {                                      // L_: [8 args] args...                           L2: func
            raise_luaL_error(L_, "error when parsing lane function code");
        }
    } else {
        raise_luaL_error(L_, "Expected function, got %s", lua_typename(L_, _func_type));
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 1);
    LUA_ASSERT(L_, lua_isfunction(_L2, 1));

    // revive arguments
    if (_nargs > 0) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "lane_new: transfer lane arguments\n" INDENT_END(_U)));
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        InterCopyResult const res{ _c.inter_move(_nargs) };                                        // L_: [8 args]                                   L2: func args...
        if (res != InterCopyResult::Success) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }
    }
    STACK_CHECK(L_, -_nargs);
    LUA_ASSERT(L_, lua_gettop(L_) == kFixedArgsIdx);

    // Store 'lane' in the lane's registry, for 'cancel_test()' (we do cancel tests at pending send/receive).
    kLanePointerRegKey.setValue(_L2, [lane = _lane](lua_State* L_) { lua_pushlightuserdata(L_, lane); });// L_: [8 args]                             L2: func args...
    STACK_CHECK(_L2, 1 + _nargs);

    STACK_CHECK_RESET_REL(L_, 0);
    // all went well, the lane's thread can start working
    onExit.success();                                                                              // L_: [8 args] lane                              L2: <living its own life>
    // we should have the lane userdata on top of the stack
    STACK_CHECK(L_, 1);
    return 1;
}

// ################################################################################################

#if HAVE_LANE_TRACKING()
//---
// threads() -> {}|nil
//
// Return a list of all known lanes
LUAG_FUNC(threads)
{
    LaneTracker const& _tracker = universe_get(L_)->tracker;
    return _tracker.pushThreadsTable(L_);
}
#endif // HAVE_LANE_TRACKING()

// #################################################################################################
// ######################################## Timer support ##########################################
// #################################################################################################

/*
 * secs = now_secs()
 *
 * Returns the current time, as seconds. Resolution depends on std::system_clock implementation
 * Can't use std::chrono::steady_clock because we need the same baseline as std::mktime
 */
LUAG_FUNC(now_secs)
{
    auto const _now{ std::chrono::system_clock::now() };
    lua_Duration duration{ _now.time_since_epoch() };

    lua_pushnumber(L_, duration.count());
    return 1;
}

// #################################################################################################

// wakeup_at_secs= wakeup_conv(date_tbl)
LUAG_FUNC(wakeup_conv)
{
    // date_tbl
    // .year (four digits)
    // .month (1..12)
    // .day (1..31)
    // .hour (0..23)
    // .min (0..59)
    // .sec (0..61)
    // .yday (day of the year)
    // .isdst (daylight saving on/off)

    STACK_CHECK_START_REL(L_, 0);
    auto _readInteger = [L = L_](char const* name_) {
        lua_getfield(L, 1, name_);
        lua_Integer const val{ lua_tointeger(L, -1) };
        lua_pop(L, 1);
        return static_cast<int>(val);
    };
    int const _year{ _readInteger("year") };
    int const _month{ _readInteger("month") };
    int const _day{ _readInteger("day") };
    int const _hour{ _readInteger("hour") };
    int const _min{ _readInteger("min") };
    int const _sec{ _readInteger("sec") };
    STACK_CHECK(L_, 0);

    // If Lua table has '.isdst' we trust that. If it does not, we'll let
    // 'mktime' decide on whether the time is within DST or not (value -1).
    //
    lua_getfield(L_, 1, "isdst");
    int const _isdst{ lua_isboolean(L_, -1) ? lua_toboolean(L_, -1) : -1 };
    lua_pop(L_, 1);
    STACK_CHECK(L_, 0);

    std::tm _t{};
    _t.tm_year = _year - 1900;
    _t.tm_mon = _month - 1; // 0..11
    _t.tm_mday = _day;      // 1..31
    _t.tm_hour = _hour;     // 0..23
    _t.tm_min = _min;       // 0..59
    _t.tm_sec = _sec;       // 0..60
    _t.tm_isdst = _isdst;   // 0/1/negative

    lua_pushnumber(L_, static_cast<lua_Number>(std::mktime(&_t))); // resolution: 1 second
    return 1;
}

// #################################################################################################
// ################################### custom allocator support ####################################
// #################################################################################################

// same as PUC-Lua l_alloc
extern "C" [[nodiscard]] static void* libc_lua_Alloc([[maybe_unused]] void* ud_, [[maybe_unused]] void* ptr_, [[maybe_unused]] size_t osize_, size_t nsize_)
{
    if (nsize_ == 0) {
        free(ptr_);
        return nullptr;
    } else {
        return realloc(ptr_, nsize_);
    }
}

// #################################################################################################

[[nodiscard]] static int luaG_provide_protected_allocator(lua_State* L_)
{
    Universe* const _U{ universe_get(L_) };
    // push a new full userdata on the stack, giving access to the universe's protected allocator
    [[maybe_unused]] AllocatorDefinition* const def{ new (L_) AllocatorDefinition{ _U->protectedAllocator.makeDefinition() } };
    return 1;
}

// #################################################################################################

// called once at the creation of the universe (therefore L is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
static void initialize_allocator_function(Universe* U_, lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    lua_getfield(L_, -1, "allocator");                                                             // L_: settings allocator|nil|"protected"
    if (!lua_isnil(L_, -1)) {
        // store C function pointer in an internal variable
        U_->provideAllocator = lua_tocfunction(L_, -1);                                            // L_: settings allocator
        if (U_->provideAllocator != nullptr) {
            // make sure the function doesn't have upvalues
            char const* upname = lua_getupvalue(L_, -1, 1);                                        // L_: settings allocator upval?
            if (upname != nullptr) {   // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "config.allocator() shouldn't have upvalues");
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings allocator nil
            lua_setfield(L_, -3, "allocator");                                                     // L_: settings allocator
        } else if (lua_type(L_, -1) == LUA_TSTRING) { // should be "protected"
            LUA_ASSERT(L_, strcmp(lua_tostring(L_, -1), "protected") == 0);
            // set the original allocator to call from inside protection by the mutex
            U_->protectedAllocator.initFrom(L_);
            U_->protectedAllocator.installIn(L_);
            // before a state is created, this function will be called to obtain the allocator
            U_->provideAllocator = luaG_provide_protected_allocator;
        }
    } else {
        // just grab whatever allocator was provided to lua_newstate
        U_->protectedAllocator.initFrom(L_);
    }
    lua_pop(L_, 1); // L_: settings
    STACK_CHECK(L_, 1);

    lua_getfield(L_, -1, "internal_allocator");                                                    // L_: settings "libc"|"allocator"
    {
        char const* const _allocator{ lua_tostring(L_, -1) };
        if (strcmp(_allocator, "libc") == 0) {
            U_->internalAllocator = AllocatorDefinition{ libc_lua_Alloc, nullptr };
        } else if (U_->provideAllocator == luaG_provide_protected_allocator) {
            // user wants mutex protection on the state's allocator. Use protection for our own allocations too, just in case.
            U_->internalAllocator = U_->protectedAllocator.makeDefinition();
        } else {
            // no protection required, just use whatever we have as-is.
            U_->internalAllocator = U_->protectedAllocator;
        }
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);
}

// #################################################################################################
// ######################################## Module linkage #########################################
// #################################################################################################

extern int LG_linda(lua_State* L_);

namespace global {
    static struct luaL_Reg const sLanesFunctions[] = {
        { "linda", LG_linda },
        { "now_secs", LG_now_secs },
        { "wakeup_conv", LG_wakeup_conv },
        { "set_thread_priority", LG_set_thread_priority },
        { "set_thread_affinity", LG_set_thread_affinity },
        { "nameof", luaG_nameof },
        { "register", LG_register },
        { "set_singlethreaded", LG_set_singlethreaded },
        { nullptr, nullptr }
    };
} // namespace global

// #################################################################################################

// upvalue 1: module name
// upvalue 2: module table
// param 1: settings table
LUAG_FUNC(configure)
{
    // start with one-time initializations.
    {
        // C++ guarantees that the static variable initialization is threadsafe.
        static auto _ = std::invoke(
            []() {
#if (defined PLATFORM_OSX) && (defined _UTILBINDTHREADTOCPU)
                chudInitialize();
#endif
                return false;
            });
    }

    Universe* _U{ universe_get(L_) };
    bool const _from_master_state{ _U == nullptr };
    char const* const _name{ luaL_checkstring(L_, lua_upvalueindex(1)) };
    LUA_ASSERT(L_, lua_type(L_, 1) == LUA_TTABLE);

    STACK_GROW(L_, 4);
    STACK_CHECK_START_ABS(L_, 1);                                                                  // L_: settings

    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%p: lanes.configure() BEGIN\n" INDENT_END(_U), L_));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ _U });

    if (_U == nullptr) {
        _U = universe_create(L_);                                                                  // L_: settings universe
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope2{ _U });
        lua_createtable(L_, 0, 1);                                                                 // L_: settings universe {mt}
        lua_getfield(L_, 1, "shutdown_timeout");                                                   // L_: settings universe {mt} shutdown_timeout
        lua_getfield(L_, 1, "shutdown_mode");                                                      // L_: settings universe {mt} shutdown_timeout shutdown_mode
        lua_pushcclosure(L_, universe_gc, 2);                                                      // L_: settings universe {mt} universe_gc
        lua_setfield(L_, -2, "__gc");                                                              // L_: settings universe {mt}
        lua_setmetatable(L_, -2);                                                                  // L_: settings universe
        lua_pop(L_, 1);                                                                            // L_: settings
        lua_getfield(L_, 1, "verbose_errors");                                                     // L_: settings verbose_errors
        _U->verboseErrors = lua_toboolean(L_, -1) ? true : false;
        lua_pop(L_, 1);                                                                            // L_: settings
        lua_getfield(L_, 1, "demote_full_userdata");                                               // L_: settings demote_full_userdata
        _U->demoteFullUserdata = lua_toboolean(L_, -1) ? true : false;
        lua_pop(L_, 1);                                                                            // L_: settings
#if HAVE_LANE_TRACKING()
        lua_getfield(L_, 1, "track_lanes");                                                        // L_: settings track_lanes
        if (lua_toboolean(L_, -1)) {
            _U->tracker.activate();
        }
        lua_pop(L_, 1);                                                                            // L_: settings
#endif // HAVE_LANE_TRACKING()
        // Linked chains handling
        _U->selfdestructFirst = SELFDESTRUCT_END;
        initialize_allocator_function(_U, L_);
        initializeOnStateCreate(_U, L_);
        init_keepers(_U, L_);
        STACK_CHECK(L_, 1);

        // Initialize 'timerLinda'; a common Linda object shared by all states
        lua_pushcfunction(L_, LG_linda);                                                           // L_: settings lanes.linda
        lua_pushliteral(L_, "lanes-timer");                                                        // L_: settings lanes.linda "lanes-timer"
        lua_call(L_, 1, 1);                                                                        // L_: settings linda
        STACK_CHECK(L_, 2);

        // Proxy userdata contents is only a 'DeepPrelude*' pointer
        _U->timerLinda = *lua_tofulluserdata<DeepPrelude*>(L_, -1);
        // increment refcount so that this linda remains alive as long as the universe exists.
        _U->timerLinda->refcount.fetch_add(1, std::memory_order_relaxed);
        lua_pop(L_, 1);                                                                            // L_: settings
    }
    STACK_CHECK(L_, 1);

    // Serialize calls to 'require' from now on, also in the primary state
    serialize_require(DEBUGSPEW_PARAM_COMMA(_U) L_);

    // Retrieve main module interface table
    lua_pushvalue(L_, lua_upvalueindex(2));                                                        // L_: settings M
    // remove configure() (this function) from the module interface
    lua_pushnil(L_);                                                                               // L_: settings M nil
    lua_setfield(L_, -2, "configure");                                                             // L_: settings M
    // add functions to the module's table
    luaG_registerlibfuncs(L_, global::sLanesFunctions);
#if HAVE_LANE_TRACKING()
    // register core.threads() only if settings say it should be available
    if (_U->tracker.isActive()) {
        lua_pushcfunction(L_, LG_threads);                                                         // L_: settings M LG_threads()
        lua_setfield(L_, -2, "threads");                                                           // L_: settings M
    }
#endif // HAVE_LANE_TRACKING()
    STACK_CHECK(L_, 2);

    {
        char const* _errmsg{
            DeepFactory::PushDeepProxy(DestState{ L_ }, _U->timerLinda, 0, LookupMode::LaneBody)
        };                                                                                         // L_: settings M timerLinda
        if (_errmsg != nullptr) {
            raise_luaL_error(L_, _errmsg);
        }
        lua_setfield(L_, -2, "timer_gateway");                                                     // L_: settings M
    }
    STACK_CHECK(L_, 2);

    // prepare the metatable for threads
    // contains keys: { __gc, __index, cached_error, cached_tostring, cancel, join, get_debug_threadname }
    Lane::PushMetatable(L_);

    lua_pushcclosure(L_, LG_lane_new, 1);                                                          // L_: settings M lane_new
    lua_setfield(L_, -2, "lane_new");                                                              // L_: settings M

    // we can't register 'lanes.require' normally because we want to create an upvalued closure
    lua_getglobal(L_, "require");                                                                  // L_: settings M require
    lua_pushcclosure(L_, LG_require, 1);                                                           // L_: settings M lanes.require
    lua_setfield(L_, -2, "require");                                                               // L_: settings M

    lua_pushfstring(
        L_,
        "%d.%d.%d",
        LANES_VERSION_MAJOR,
        LANES_VERSION_MINOR,
        LANES_VERSION_PATCH
    );                                                                                             // L_: settings M VERSION
    lua_setfield(L_, -2, "version");                                                               // L_: settings M

    lua_pushinteger(L_, kThreadPrioMax);                                                           // L_: settings M kThreadPrioMax
    lua_setfield(L_, -2, "max_prio");                                                              // L_: settings M

    kCancelError.pushKey(L_);                                                                      // L_: settings M kCancelError
    lua_setfield(L_, -2, "cancel_error");                                                          // L_: settings M

    kNilSentinel.pushKey(L_);                                                                      // L_: settings M kNilSentinel
    lua_setfield(L_, -2, "null");                                                                  // L_: settings M

    STACK_CHECK(L_, 2); // reference stack contains only the function argument 'settings'
    // we'll need this every time we transfer some C function from/to this state
    kLookupRegKey.setValue(L_, [](lua_State* L_) { lua_newtable(L_); });                           // L_: settings M
    STACK_CHECK(L_, 2);

    // register all native functions found in that module in the transferable functions database
    // we process it before _G because we don't want to find the module when scanning _G (this would generate longer names)
    // for example in package.loaded["lanes.core"].*
    populate_func_lookup_table(L_, -1, _name);
    STACK_CHECK(L_, 2);

    // record all existing C/JIT-fast functions
    // Lua 5.2 no longer has LUA_GLOBALSINDEX: we must push globals table on the stack
    if (_from_master_state) {
        // don't do this when called during the initialization of a new lane,
        // because we will do it after on_state_create() is called,
        // and we don't want to skip _G because of caching in case globals are created then
        lua_pushglobaltable(L_);                                                                   // L_: settings M _G
        populate_func_lookup_table(L_, -1, nullptr);
        lua_pop(L_, 1);                                                                            // L_: settings M
    }
    lua_pop(L_, 1);                                                                                // L_: settings

    // set _R[kConfigRegKey] = settings
    kConfigRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    STACK_CHECK(L_, 1);
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%p: lanes.configure() END\n" INDENT_END(_U), L_));
    // Return the settings table
    return 1;
}

// #################################################################################################

#if defined PLATFORM_WIN32 && !defined NDEBUG
#include <signal.h>
#include <conio.h>

void signal_handler(int signal_)
{
    if (signal_ == SIGABRT) {
        _cprintf("caught abnormal termination!");
        abort();
    }
}

// #################################################################################################

// helper to have correct callstacks when crashing a Win32 running on 64 bits Windows
// don't forget to toggle Debug/Exceptions/Win32 in visual Studio too!
static volatile long s_ecoc_initCount = 0;
static volatile int s_ecoc_go_ahead = 0;
static void EnableCrashingOnCrashes(void)
{
    if (InterlockedCompareExchange(&s_ecoc_initCount, 1, 0) == 0) {
        typedef BOOL(WINAPI * tGetPolicy)(LPDWORD lpFlags);
        typedef BOOL(WINAPI * tSetPolicy)(DWORD dwFlags);
        const DWORD EXCEPTION_SWALLOWING = 0x1;

        HMODULE _kernel32 = LoadLibraryA("kernel32.dll");
        if (_kernel32) {
            tGetPolicy pGetPolicy = (tGetPolicy) GetProcAddress(_kernel32, "GetProcessUserModeExceptionPolicy");
            tSetPolicy pSetPolicy = (tSetPolicy) GetProcAddress(_kernel32, "SetProcessUserModeExceptionPolicy");
            if (pGetPolicy && pSetPolicy) {
                DWORD _dwFlags;
                if (pGetPolicy(&_dwFlags)) {
                    // Turn off the filter
                    pSetPolicy(_dwFlags & ~EXCEPTION_SWALLOWING);
                }
            }
            FreeLibrary(_kernel32);
        }
        // typedef void (* SignalHandlerPointer)( int);
        /*SignalHandlerPointer previousHandler =*/signal(SIGABRT, signal_handler);

        s_ecoc_go_ahead = 1; // let others pass
    } else {
        while (!s_ecoc_go_ahead) {
            Sleep(1);
        } // changes threads
    }
}
#endif // PLATFORM_WIN32 && !defined NDEBUG

// #################################################################################################

LANES_API int luaopen_lanes_core(lua_State* L_)
{
#if defined PLATFORM_WIN32 && !defined NDEBUG
    EnableCrashingOnCrashes();
#endif // defined PLATFORM_WIN32 && !defined NDEBUG

    STACK_GROW(L_, 4);
    STACK_CHECK_START_REL(L_, 0);

    // Prevent PUC-Lua/LuaJIT mismatch. Hopefully this works for MoonJIT too
#if LUAJIT_FLAVOR() == 0
    if (luaG_getmodule(L_, LUA_JITLIBNAME) != LuaType::NIL)
        raise_luaL_error(L_, "Lanes is built for PUC-Lua, don't run from LuaJIT");
#else
    if (luaG_getmodule(L_, LUA_JITLIBNAME) == LuaType::NIL)
        raise_luaL_error(L_, "Lanes is built for LuaJIT, don't run from PUC-Lua");
#endif
    lua_pop(L_, 1);                                                                                // L_:
    STACK_CHECK(L_, 0);

    // Create main module interface table
    // we only have 1 closure, which must be called to configure Lanes
    lua_newtable(L_);                                                                              // L_: M
    lua_pushvalue(L_, 1);                                                                          // L_: M "lanes.core"
    lua_pushvalue(L_, -2);                                                                         // L_: M "lanes.core" M
    lua_pushcclosure(L_, LG_configure, 2);                                                         // L_: M LG_configure()
    kConfigRegKey.pushValue(L_);                                                                   // L_: M LG_configure() settings
    if (!lua_isnil(L_, -1)) { // this is not the first require "lanes.core": call configure() immediately
        lua_pushvalue(L_, -1);                                                                     // L_: M LG_configure() settings settings
        lua_setfield(L_, -4, "settings");                                                          // L_: M LG_configure() settings
        lua_call(L_, 1, 0);                                                                        // L_: M
    } else {
        // will do nothing on first invocation, as we haven't stored settings in the registry yet
        lua_setfield(L_, -3, "settings");                                                          // L_: M LG_configure()
        lua_setfield(L_, -2, "configure");                                                         // L_: M
    }

    STACK_CHECK(L_, 1);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int default_luaopen_lanes(lua_State* L_)
{
    int const _rc{ luaL_loadfile(L_, "lanes.lua") || lua_pcall(L_, 0, 1, 0) };
    if (_rc != LUA_OK) {
        raise_luaL_error(L_, "failed to initialize embedded Lanes");
    }
    return 1;
}

// #################################################################################################

// call this instead of luaopen_lanes_core() when embedding Lua and Lanes in a custom application
LANES_API void luaopen_lanes_embedded(lua_State* L_, lua_CFunction _luaopen_lanes)
{
    STACK_CHECK_START_REL(L_, 0);
    // pre-require lanes.core so that when lanes.lua calls require "lanes.core" it finds it is already loaded
    luaL_requiref(L_, "lanes.core", luaopen_lanes_core, 0);                                        // L_: ... lanes.core
    lua_pop(L_, 1);                                                                                // L_: ...
    STACK_CHECK(L_, 0);
    // call user-provided function that runs the chunk "lanes.lua" from wherever they stored it
    luaL_requiref(L_, "lanes", _luaopen_lanes ? _luaopen_lanes : default_luaopen_lanes, 0);        // L_: ... lanes
    STACK_CHECK(L_, 1);
}
