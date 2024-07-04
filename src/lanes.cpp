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

#include "_pch.h"
#include "lanes.h"

#include "deep.h"
#include "intercopycontext.h"
#include "keeper.h"
#include "lane.h"
#include "nameof.h"
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
    THREAD_SET_PRIORITY(static_cast<int>(_prio), Universe::Get(L_)->sudo);
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

LUAG_FUNC(sleep)
{
    extern LUAG_FUNC(linda_receive);

    Universe* const _U{ Universe::Get(L_) };
    lua_settop(L_, 1);
    lua_pushcfunction(L_, LG_linda_receive);                                                       // L_: duration|nil receive()
    STACK_CHECK_START_REL(L_, 0); // we pushed the function we intend to call, now prepare the arguments
    _U->timerLinda->push(L_);                                                                      // L_: duration|nil receive() timerLinda
    if (luaG_tostring(L_, 1) == "indefinitely") {
        lua_pushnil(L_);                                                                           // L_: duration? receive() timerLinda nil
    } else if (lua_isnoneornil(L_, 1)) {
        lua_pushnumber(L_, 0);                                                                     // L_: duration? receive() timerLinda 0
    } else if (!lua_isnumber(L_, 1)) {
        raise_luaL_argerror(L_, 1, "invalid duration");
    }
    else {
        lua_pushnumber(L_, lua_tonumber(L_, 1));                                                   // L_: duration? receive() timerLinda duration
    }
    luaG_pushstring(L_, "ac100de1-a696-4619-b2f0-a26de9d58ab8");                                   // L_: duration? receive() timerLinda duration key
    STACK_CHECK(L_, 3); // 3 arguments ready
    lua_call(L_, 3, LUA_MULTRET); // timerLinda:receive(duration,key)                              // L_: duration? result...
    return lua_gettop(L_) - 1;
}

// #################################################################################################

// --- If a client wants to transfer stuff of a given module from the current state to another Lane, the module must be required
// with lanes.require, that will call the regular 'require', then populate the lookup database in the source lane
// module = lanes.require( "modname")
// upvalue[1]: _G.require
LUAG_FUNC(require)
{
    std::string_view const _name{ luaG_tostring(L_, 1) };                                          // L_: "name" ...
    int const _nargs{ lua_gettop(L_) };
    DEBUGSPEW_CODE(Universe * _U{ Universe::Get(L_) });
    STACK_CHECK_START_REL(L_, 0);
    DEBUGSPEW_CODE(DebugSpew(_U) << "lanes.require '" << _name << "' BEGIN" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
    lua_pushvalue(L_, lua_upvalueindex(1));                                                        // L_: "name" ... require
    lua_insert(L_, 1);                                                                             // L_: require "name" ...
    lua_call(L_, _nargs, 1);                                                                       // L_: module
    tools::PopulateFuncLookupTable(L_, -1, _name);
    DEBUGSPEW_CODE(DebugSpew(_U) << "lanes.require '" << _name << "' END" << std::endl);
    STACK_CHECK(L_, 0);
    return 1;
}

// #################################################################################################

// --- If a client wants to transfer stuff of a previously required module from the current state to another Lane, the module must be registered
// to populate the lookup database in the source lane (and in the destination too, of course)
// lanes.register( "modname", module)
LUAG_FUNC(register)
{
    std::string_view const _name{ luaG_checkstring(L_, 1) };
    LuaType const _mod_type{ luaG_type(L_, 2) };
    // ignore extra arguments, just in case
    lua_settop(L_, 2);
    luaL_argcheck(L_, (_mod_type == LuaType::TABLE) || (_mod_type == LuaType::FUNCTION), 2, "unexpected module type");
    DEBUGSPEW_CODE(Universe* _U = Universe::Get(L_));
    STACK_CHECK_START_REL(L_, 0); // "name" mod_table
    DEBUGSPEW_CODE(DebugSpew(_U) << "lanes.register '" << _name << "' BEGIN" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
    tools::PopulateFuncLookupTable(L_, -1, _name);
    DEBUGSPEW_CODE(DebugSpew(_U) << "lanes.register '" << _name << "' END" << std::endl);
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

//--- [] means can be nil
// lane_ud = lane_new( function
//                   , [libs_str]
//                   , [priority_int]
//                   , [globals_tbl]
//                   , [package_tbl]
//                   , [required_tbl]
//                   , [gc_cb_func]
//                   , [name]
//                   , error_trace_level
//                   , as_coroutine
//                  [, ... args ...])
//
// Upvalues: metatable to use for 'lane_ud'
//
LUAG_FUNC(lane_new)
{
    static constexpr int kFuncIdx{ 1 };
    static constexpr int kLibsIdx{ 2 };
    static constexpr int kPrioIdx{ 3 };
    static constexpr int kGlobIdx{ 4 };
    static constexpr int kPackIdx{ 5 };
    static constexpr int kRequIdx{ 6 };
    static constexpr int kGcCbIdx{ 7 };
    static constexpr int kNameIdx{ 8 };
    static constexpr int kErTlIdx{ 9 };
    static constexpr int kAsCoro{ 10 };
    static constexpr int kFixedArgsIdx{ 10 };

    int const _nargs{ lua_gettop(L_) - kFixedArgsIdx };
    LUA_ASSERT(L_, _nargs >= 0);

    Universe* const _U{ Universe::Get(L_) };
    DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: setup" << std::endl);

    std::optional<std::string_view> _libs_str{ lua_isnil(L_, kLibsIdx) ? std::nullopt : std::make_optional(luaG_tostring(L_, kLibsIdx)) };
    lua_State* const _S{ state::NewLaneState(_U, SourceState{ L_ }, _libs_str) };                 // L_: [fixed] ...                                L2:
    STACK_CHECK_START_REL(_S, 0);

    // 'lane' is allocated from heap, not Lua, since its life span may surpass the handle's (if free running thread)
    Lane::ErrorTraceLevel const _errorTraceLevel{ static_cast<Lane::ErrorTraceLevel>(lua_tointeger(L_, kErTlIdx)) };
    bool const _asCoroutine{ lua_toboolean(L_, kAsCoro) ? true : false };
    Lane* const _lane{ new (_U) Lane{ _U, _S, _errorTraceLevel, _asCoroutine } };
    STACK_CHECK(_S, _asCoroutine ? 1 : 0); // the Lane's thread is on the Lane's state stack
    lua_State* const _L2{ _lane->L };
    STACK_CHECK_START_REL(_L2, 0);
    if (_lane == nullptr) {
        raise_luaL_error(L_, "could not create lane: out of memory");
    }

    class OnExit
    {
        private:
        lua_State* const L;
        Lane* lane{ nullptr };
        DEBUGSPEW_CODE(DebugSpewIndentScope scope);

        public:
        OnExit(lua_State* L_, Lane* lane_)
        : L{ L_ }
        , lane{ lane_ }
        DEBUGSPEW_COMMA_PARAM(scope{ lane_->U })
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
#ifndef __PROSPERO__
                lane->ready.count_down();
#else // __PROSPERO__
                lane->ready.test_and_set();
#endif // __PROSPERO__
            }
        }

        private:
        void prepareUserData()
        {
            DEBUGSPEW_CODE(DebugSpew(lane->U) << "lane_new: preparing lane userdata" << std::endl);
            STACK_CHECK_START_REL(L, 0);
            // a Lane full userdata needs a single uservalue
            Lane** const _ud{ luaG_newuserdatauv<Lane*>(L, 1) };                                   // L: ... lane
            *_ud = lane; // don't forget to store the pointer in the userdata!

            // Set metatable for the userdata
            lua_pushvalue(L, lua_upvalueindex(1));                                                 // L: ... lane mt
            lua_setmetatable(L, -2);                                                               // L: ... lane
            STACK_CHECK(L, 1);

            // Create uservalue for the userdata. There can be only one that must be a table, due to Lua 5.1 compatibility.
            // (this is where lane body return values will be stored when the handle is indexed by a numeric key)
            lua_newtable(L);                                                                       // L: ... lane {uv}

            // Store the gc_cb callback in the uservalue
            int const _gc_cb_idx{ lua_isnoneornil(L, kGcCbIdx) ? 0 : kGcCbIdx };
            if (_gc_cb_idx > 0) {
                kLaneGC.pushKey(L);                                                                // L: ... lane {uv} k
                lua_pushvalue(L, _gc_cb_idx);                                                      // L: ... lane {uv} k gc_cb
                lua_rawset(L, -3);                                                                 // L: ... lane {uv}
            }
            STACK_CHECK(L, 2);
            // store the uservalue in the Lane full userdata
            lua_setiuservalue(L, -2, 1);                                                           // L: ... lane

            lua_State* const _L2{ lane->L };
            STACK_CHECK_START_REL(_L2, 0);
            int const _name_idx{ lua_isnoneornil(L, kNameIdx) ? 0 : kNameIdx };
            std::string_view const _debugName{ (_name_idx > 0) ? luaG_tostring(L, _name_idx) : std::string_view{} };
            if (!_debugName.empty())
            {
                if (_debugName != "auto") {
                    luaG_pushstring(_L2, _debugName);                                              // L: ... lane                                    L2: "<name>"
                } else {
                    lua_Debug _ar;
                    lua_pushvalue(L, kFuncIdx);                                                    // L: ... lane func
                    lua_getinfo(L, ">S", &_ar);                                                    // L: ... lane
                    luaG_pushstring(_L2, "%s:%d", _ar.short_src, _ar.linedefined);                 // L: ... lane                                    L2: "<name>"
                }
                lane->changeDebugName(-1);
                lua_pop(_L2, 1);                                                                   // L: ... lane                                    L2:
            }
            STACK_CHECK(_L2, 0);
            STACK_CHECK(L, 1);
        }

        public:
        void success()
        {
            prepareUserData();
            // unblock the thread so that it can terminate gracefully
#ifndef __PROSPERO__
            lane->ready.count_down();
#else // __PROSPERO__
            lane->ready.test_and_set();
#endif // __PROSPERO__
            lane = nullptr;
        }
    } _onExit{ L_, _lane};
    // launch the thread early, it will sync with a std::latch to parallelize OS thread warmup and L2 preparation
    DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: launching thread" << std::endl);
    // public Lanes API accepts a generic range -3/+3
    // that will be remapped into the platform-specific scheduler priority scheme
    // On some platforms, -3 is equivalent to -2 and +3 to +2
    int const _priority{
        std::invoke([L = L_]() {
            int const _prio_idx{ lua_isnoneornil(L, kPrioIdx) ? 0 : kPrioIdx };
            if (_prio_idx == 0) {
                return kThreadPrioDefault;
            }
            int const _priority{ static_cast<int>(lua_tointeger(L, _prio_idx)) };
            if ((_priority < kThreadPrioMin || _priority > kThreadPrioMax)) {
                raise_luaL_error(L, "Priority out of range: %d..+%d (%d)", kThreadPrioMin, kThreadPrioMax, _priority);
            }
            return _priority;
        })
    };

    _lane->startThread(_priority);

    STACK_GROW(_L2, _nargs + 3);
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);

    // package
    int const _package_idx{ lua_isnoneornil(L_, kPackIdx) ? 0 : kPackIdx };
    if (_package_idx != 0) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: update 'package'" << std::endl);
        // when copying with mode LookupMode::LaneBody, should raise an error in case of problem, not leave it one the stack
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, SourceIndex{ _package_idx }, {}, {}, {} };
        [[maybe_unused]] InterCopyResult const _ret{ _c.interCopyPackage() };
        LUA_ASSERT(L_, _ret == InterCopyResult::Success); // either all went well, or we should not even get here
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 0);

    // modules to require in the target lane *before* the function is transfered!
    int const _required_idx{ lua_isnoneornil(L_, kRequIdx) ? 0 : kRequIdx };
    if (_required_idx != 0) {
        int _nbRequired{ 1 };
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: process 'required' list" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        // should not happen, was checked in lanes.lua before calling lane_new()
        if (luaG_type(L_, _required_idx) != LuaType::TABLE) {
            raise_luaL_error(L_, "expected required module list as a table, got %s", luaL_typename(L_, _required_idx));
        }

        lua_pushnil(L_);                                                                           // L_: [fixed] args... nil                        L2:
        while (lua_next(L_, _required_idx) != 0) {                                                 // L_: [fixed] args... n "modname"                L2:
            if (luaG_type(L_, -1) != LuaType::STRING || luaG_type(L_, -2) != LuaType::NUMBER || lua_tonumber(L_, -2) != _nbRequired) {
                raise_luaL_error(L_, "required module list should be a list of strings");
            } else {
                // require the module in the target state, and populate the lookup table there too
                std::string_view const _name{ luaG_tostring(L_, -1) };
                DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: require '" << _name << "'" << std::endl);

                // require the module in the target lane
                lua_getglobal(_L2, "require");                                                     // L_: [fixed] args... n "modname"                L2: require()?
                if (lua_isnil(_L2, -1)) {
                    lua_pop(_L2, 1);                                                               // L_: [fixed] args... n "modname"                L2:
                    raise_luaL_error(L_, "cannot pre-require modules without loading 'package' library first");
                } else {
                    luaG_pushstring(_L2, _name);                                                   // L_: [fixed] args... n "modname"                L2: require() name
                    LuaError const _rc{ lua_pcall(_L2, 1, 1, 0) };                                 // L_: [fixed] args... n "modname"                L2: ret/errcode
                    if (_rc != LuaError::OK) {
                        // propagate error to main state if any
                        InterCopyContext _c{ _U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} };
                        std::ignore = _c.interMove(1);                                             // L_: [fixed] args... n "modname" error          L2:
                        raise_lua_error(L_);
                    }
                    // here the module was successfully required                                   // L_: [fixed] args... n "modname"                L2: ret
                    // after requiring the module, register the functions it exported in our name<->function database
                    tools::PopulateFuncLookupTable(_L2, -1, _name);
                    lua_pop(_L2, 1);                                                               // L_: [fixed] args... n "modname"                L2:
                }
            }
            lua_pop(L_, 1);                                                                        // L_: [fixed] args... n                          L2:
            ++_nbRequired;
        }                                                                                          // L_: [fixed] args...
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 0);                                                                           // L_: [fixed] args...                            L2:

    // Appending the specified globals to the global environment
    // *after* stdlibs have been loaded and modules required, in case we transfer references to native functions they exposed...
    //
    int const _globals_idx{ lua_isnoneornil(L_, kGlobIdx) ? 0 : kGlobIdx };
    if (_globals_idx != 0) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: transfer globals" << std::endl);
        if (!lua_istable(L_, _globals_idx)) {
            raise_luaL_error(L_, "Expected table, got %s", luaL_typename(L_, _globals_idx));
        }

        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        lua_pushnil(L_);                                                                           // L_: [fixed] args... nil                        L2:
        // Lua 5.2 wants us to push the globals table on the stack
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        luaG_pushglobaltable(_L2);                                                                 // L_: [fixed] args... nil                        L2: _G
        while (lua_next(L_, _globals_idx)) {                                                       // L_: [fixed] args... k v                        L2: _G
            std::ignore = _c.interCopy(2);                                                         // L_: [fixed] args... k v                        L2: _G k v
            // assign it in L2's globals table
            lua_rawset(_L2, -3);                                                                   // L_: [fixed] args... k v                        L2: _G
            lua_pop(L_, 1);                                                                        // L_: [fixed] args... k
        }                                                                                          // L_: [fixed] args...
        lua_pop(_L2, 1);                                                                           // L_: [fixed] args...                            L2:
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, 0);

    // Lane main function
    [[maybe_unused]] int const _errorHandlerCount{ _lane->pushErrorHandler() };                    // L_: [fixed] args...                            L2: eh?
    LuaType const _func_type{ luaG_type(L_, kFuncIdx) };
    if (_func_type == LuaType::FUNCTION) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: transfer lane body" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        lua_pushvalue(L_, kFuncIdx);                                                               // L_: [fixed] args... func                       L2: eh?
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        InterCopyResult const _res{ _c.interMove(1) };                                             // L_: [fixed] args...                            L2: eh? func
        if (_res != InterCopyResult::Success) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }
    } else if (_func_type == LuaType::STRING) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: compile lane body" << std::endl);
        // compile the string
        if (luaL_loadstring(_L2, lua_tostring(L_, kFuncIdx)) != 0) {                               // L_: [fixed] args...                            L2: eh? func
            raise_luaL_error(L_, "error when parsing lane function code");
        }
    } else {
        raise_luaL_error(L_, "Expected function, got %s", luaG_typename(L_, _func_type).data());
    }
    STACK_CHECK(L_, 0);
    STACK_CHECK(_L2, _errorHandlerCount + 1);
    LUA_ASSERT(L_, lua_isfunction(_L2, _errorHandlerCount + 1));

    // revive arguments
    if (_nargs > 0) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "lane_new: transfer lane arguments" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        InterCopyContext _c{ _U, DestState{ _L2 }, SourceState{ L_ }, {}, {}, {}, {}, {} };
        InterCopyResult const res{ _c.interMove(_nargs) };                                         // L_: [fixed]                                    L2: eh? func args...
        if (res != InterCopyResult::Success) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }
    }
    STACK_CHECK(L_, -_nargs);
    LUA_ASSERT(L_, lua_gettop(L_) == kFixedArgsIdx);
    STACK_CHECK(_L2, _errorHandlerCount + 1 + _nargs);

    // Store 'lane' in the lane's registry, for 'cancel_test()' (we do cancel tests at pending send/receive).
    kLanePointerRegKey.setValue(
        _L2, [lane = _lane](lua_State* L_) { lua_pushlightuserdata(L_, lane); }                    // L_: [fixed]                                    L2: eh? func args...
    );
    STACK_CHECK(_L2, _errorHandlerCount + 1 + _nargs);

    // if in coroutine mode, the Lane's master state stack should contain the thread
    if (_asCoroutine) {
        LUA_ASSERT(L_, _S != _L2);
        STACK_CHECK(_S, 1);
    }
    // and the thread's stack has whatever is needed to run
    STACK_CHECK(_L2, _errorHandlerCount + 1 + _nargs);

    STACK_CHECK_RESET_REL(L_, 0);
    // all went well, the lane's thread can start working
    _onExit.success();                                                                             // L_: [fixed] lane                               L2: <living its own life>
    // we should have the lane userdata on top of the stack
    STACK_CHECK(L_, 1);
    return 1;
}

// #################################################################################################

// threads() -> {}|nil
// Return a list of all known lanes
LUAG_FUNC(threads)
{
    LaneTracker const& _tracker = Universe::Get(L_)->tracker;
    return _tracker.pushThreadsTable(L_);
}

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
    auto _readInteger = [L = L_](std::string_view const& name_) {
        std::ignore = luaG_getfield(L, 1, name_);
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
    int const _isdst{ (luaG_getfield(L_, 1, "isdst") == LuaType::BOOLEAN) ? lua_toboolean(L_, -1) : -1 };
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
// ######################################## Module linkage #########################################
// #################################################################################################

extern LUAG_FUNC(linda);

namespace {
    namespace local {
        static struct luaL_Reg const sLanesFunctions[] = {
            { Universe::kFinally, Universe::InitializeFinalizer },
            { "linda", LG_linda },
            { "nameof", LG_nameof },
            { "now_secs", LG_now_secs },
            { "register", LG_register },
            { "set_singlethreaded", LG_set_singlethreaded },
            { "set_thread_priority", LG_set_thread_priority },
            { "set_thread_affinity", LG_set_thread_affinity },
            { "sleep", LG_sleep },
            { "supported_libs", state::LG_supported_libs },
            { "wakeup_conv", LG_wakeup_conv },
            { nullptr, nullptr }
        };
    } // namespace local
} // namespace

// #################################################################################################

// upvalue 1: module name
// upvalue 2: module table
// param 1: settings table
LUAG_FUNC(configure)
{
    // start with one-time initializations.
    {
        // C++ guarantees that the static variable initialization is threadsafe.
        [[maybe_unused]] static auto _ = std::invoke(
            []() {
#if (defined PLATFORM_OSX) && (defined _UTILBINDTHREADTOCPU)
                chudInitialize();
#endif
                return false;
            });
    }

    Universe* _U{ Universe::Get(L_) };
    bool const _from_master_state{ _U == nullptr };
    std::string_view const _name{ luaG_checkstring(L_, lua_upvalueindex(1)) };
    LUA_ASSERT(L_, luaG_type(L_, 1) == LuaType::TABLE);

    STACK_GROW(L_, 4);
    STACK_CHECK_START_ABS(L_, 1);                                                                  // L_: settings

    DEBUGSPEW_CODE(DebugSpew(_U) << L_ << ": lanes.configure() BEGIN" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });

    if (_U == nullptr) {
        // store a hidden reference in the registry to make sure the string is kept around even if a lane decides to manually change the "decoda_name" global...
        kLaneNameRegKey.setValue(L_, [](lua_State* L_) { luaG_pushstring(L_, "main"); });

        // create the universe
        _U = Universe::Create(L_);                                                                 // L_: settings universe
    }
    STACK_CHECK(L_, 1);

    // Serialize calls to 'require' from now on, also in the primary state
    tools::SerializeRequire(L_);

    // Retrieve main module interface table
    lua_pushvalue(L_, lua_upvalueindex(2));                                                        // L_: settings M
    // remove configure() (this function) from the module interface
    lua_pushnil(L_);                                                                               // L_: settings M nil
    lua_setfield(L_, -2, "configure");                                                             // L_: settings M
    // add functions to the module's table
    luaG_registerlibfuncs(L_, local::sLanesFunctions);

    // register core.threads() only if settings say it should be available
    if (_U->tracker.isActive()) {
        lua_pushcfunction(L_, LG_threads);                                                         // L_: settings M LG_threads()
        lua_setfield(L_, -2, "threads");                                                           // L_: settings M
    }

    STACK_CHECK(L_, 2);
    DeepFactory::PushDeepProxy(DestState{ L_ }, _U->timerLinda, 0, LookupMode::LaneBody, L_);      // L_: settings M timerLinda
    lua_setfield(L_, -2, "timerLinda");                                                            // L_: settings M
    STACK_CHECK(L_, 2);

    // prepare the metatable for threads
    // contains keys: { __gc, __index, cancel, join, get_threadname }
    Lane::PushMetatable(L_);                                                                       // L_: settings M {lane_mt}
    lua_pushcclosure(L_, LG_lane_new, 1);                                                          // L_: settings M lane_new
    lua_setfield(L_, -2, "lane_new");                                                              // L_: settings M

    // we can't register 'lanes.require' normally because we want to create an upvalued closure
    lua_getglobal(L_, "require");                                                                  // L_: settings M require
    lua_pushcclosure(L_, LG_require, 1);                                                           // L_: settings M lanes.require
    lua_setfield(L_, -2, "require");                                                               // L_: settings M

    luaG_pushstring(
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
    tools::PopulateFuncLookupTable(L_, -1, _name);
    STACK_CHECK(L_, 2);

    // record all existing C/JIT-fast functions
    // Lua 5.2 no longer has LUA_GLOBALSINDEX: we must push globals table on the stack
    if (_from_master_state) {
        // don't do this when called during the initialization of a new lane,
        // because we will do it after on_state_create() is called,
        // and we don't want to skip _G because of caching in case globals are created then
        luaG_pushglobaltable(L_);                                                                  // L_: settings M _G
        tools::PopulateFuncLookupTable(L_, -1, {});
        lua_pop(L_, 1);                                                                            // L_: settings M
    }
    lua_pop(L_, 1);                                                                                // L_: settings

    // set _R[kConfigRegKey] = settings
    kConfigRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    STACK_CHECK(L_, 1);
    DEBUGSPEW_CODE(DebugSpew(_U) << L_ << ": lanes.configure() END" << std::endl);
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
    if constexpr (LUAJIT_FLAVOR() == 0) {
        if (luaG_getmodule(L_, LUA_JITLIBNAME) != LuaType::NIL)
            raise_luaL_error(L_, "Lanes is built for PUC-Lua, don't run from LuaJIT");
    } else {
        if (luaG_getmodule(L_, LUA_JITLIBNAME) == LuaType::NIL)
            raise_luaL_error(L_, "Lanes is built for LuaJIT, don't run from PUC-Lua");
    }
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
    LuaError const _rc{ luaL_loadfile(L_, "lanes.lua") || lua_pcall(L_, 0, 1, 0) };
    if (_rc != LuaError::OK) {
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
    luaL_requiref(L_, kLanesCoreLibName, luaopen_lanes_core, 0);                                   // L_: ... lanes.core
    lua_pop(L_, 1);                                                                                // L_: ...
    STACK_CHECK(L_, 0);
    // call user-provided function that runs the chunk "lanes.lua" from wherever they stored it
    luaL_requiref(L_, kLanesLibName, _luaopen_lanes ? _luaopen_lanes : default_luaopen_lanes, 0);  // L_: ... lanes
    STACK_CHECK(L_, 1);
}
