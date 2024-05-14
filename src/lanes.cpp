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
#include "lanes_private.h"
#include "state.h"
#include "threading.h"
#include "tools.h"
#include "universe.h"

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

Lane::Lane(Universe* U_, lua_State* L_)
: U{ U_ }
, L{ L_ }
{
#if HAVE_LANE_TRACKING()
    U->tracker.tracking_add(this);
#endif // HAVE_LANE_TRACKING()
}

// #################################################################################################

bool Lane::waitForCompletion(std::chrono::time_point<std::chrono::steady_clock> until_)
{
    std::unique_lock _guard{ doneMutex };
    // std::stop_token token{ thread.get_stop_token() };
    // return doneCondVar.wait_until(lock, token, secs_, [this](){ return status >= Lane::Done; });
    return doneCondVar.wait_until(_guard, until_, [this]() { return status >= Lane::Done; });
}

// #################################################################################################

static void lane_main(Lane* lane_);
void Lane::startThread(int priority_)
{
    thread = std::jthread([this]() { lane_main(this); });
    if (priority_ != kThreadPrioDefault) {
        JTHREAD_SET_PRIORITY(thread, priority_, U->sudo);
    }
}

// #################################################################################################

/* Do you want full call stacks, or just the line where the error happened?
 *
 * TBD: The full stack feature does not seem to work (try 'make error').
 */
#define ERROR_FULL_STACK 1 // must be either 0 or 1 as we do some index arithmetics with it!

// intern the debug name in the caller lua state so that the pointer remains valid after the lane's state is closed
void Lane::securizeDebugName(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 0);
    STACK_GROW(L_, 3);
    // a Lane's uservalue should be a table
    lua_getiuservalue(L_, 1, 1);                                                                   // L_: lane ... {uv}
    LUA_ASSERT(L_, lua_istable(L_, -1));
    // we don't care about the actual key, so long as it's unique and can't collide with anything.
    lua_newtable(L_);                                                                              // L_: lane ... {uv} {}
    // Lua 5.1 can't do 'lane_->debugName = lua_pushstring(L_, lane_->debugName);'
    lua_pushstring(L_, debugName);                                                                 // L_: lane ... {uv} {} name
    debugName = lua_tostring(L_, -1);
    lua_rawset(L_, -3);                                                                            // L_: lane ... {uv}
    lua_pop(L_, 1);                                                                                // L_: lane
    STACK_CHECK(L_, 0);
}

// #################################################################################################

#if ERROR_FULL_STACK
[[nodiscard]] static int lane_error(lua_State* L_);
// xxh64 of string "kStackTraceRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kStackTraceRegKey{ 0x3F327747CACAA904ull };
#endif // ERROR_FULL_STACK

/*
 * registry[FINALIZER_REG_KEY] is either nil (no finalizers) or a table
 * of functions that Lanes will call after the executing 'pcall' has ended.
 *
 * We're NOT using the GC system for finalizer mainly because providing the
 * error (and maybe stack trace) parameters to the finalizer functions would
 * anyways complicate that approach.
 */
// xxh64 of string "kFinalizerRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kFinalizerRegKey{ 0xFE936BFAA718FEEAull };

// #################################################################################################

Lane::~Lane()
{
    // Clean up after a (finished) thread
    //
#if HAVE_LANE_TRACKING()
    std::ignore = U->tracker.tracking_remove(this);
#endif // HAVE_LANE_TRACKING()
}

// #################################################################################################
// ########################################## Finalizer ############################################
// #################################################################################################

// void= finalizer( finalizer_func )
//
// finalizer_func( [err, stack_tbl] )
//
// Add a function that will be called when exiting the lane, either via
// normal return or an error.
//
LUAG_FUNC(set_finalizer)
{
    luaL_argcheck(L_, lua_isfunction(L_, 1), 1, "finalizer should be a function");
    luaL_argcheck(L_, lua_gettop(L_) == 1, 1, "too many arguments");
    STACK_GROW(L_, 3);
    // Get the current finalizer table (if any), create one if it doesn't exist
    std::ignore = kFinalizerRegKey.getSubTable(L_, 1, 0);                                          // L_: finalizer {finalisers}
    // must cast to int, not lua_Integer, because LuaJIT signature of lua_rawseti is not the same as PUC-Lua.
    int const _idx{ static_cast<int>(lua_rawlen(L_, -1) + 1) };
    lua_pushvalue(L_, 1);                                                                          // L_: finalizer {finalisers} finalizer
    lua_rawseti(L_, -2, _idx);                                                                     // L_: finalizer {finalisers}
    // no need to adjust the stack, Lua does this for us
    return 0;
}

// #################################################################################################

static void push_stack_trace(lua_State* L_, int rc_, int stk_base_)
{
    // Lua 5.1 error handler is limited to one return value; it stored the stack trace in the registry
    switch (rc_) {
    case LUA_OK: // no error, body return values are on the stack
        break;

    case LUA_ERRRUN: // cancellation or a runtime error
#if ERROR_FULL_STACK // when ERROR_FULL_STACK, we installed a handler
        {
            STACK_CHECK_START_REL(L_, 0);
            // fetch the call stack table from the registry where the handler stored it
            STACK_GROW(L_, 1);
            // yields nil if no stack was generated (in case of cancellation for example)
            kStackTraceRegKey.pushValue(L_);                                                       // L_: err trace|nil
            STACK_CHECK(L_, 1);

            // For cancellation the error message is kCancelError, and a stack trace isn't placed
            // For other errors, the message can be whatever was thrown, and we should have a stack trace table
            LUA_ASSERT(L_, lua_type(L_, 1 + stk_base_) == (kCancelError.equals(L_, stk_base_) ? LUA_TNIL : LUA_TTABLE));
            // Just leaving the stack trace table on the stack is enough to get it through to the master.
            break;
        }
#else // !ERROR_FULL_STACK
        [[fallthrough]]; // fall through if not ERROR_FULL_STACK
#endif // !ERROR_FULL_STACK

    case LUA_ERRMEM: // memory allocation error (handler not called)
    case LUA_ERRERR: // error while running the error handler (if any, for example an out-of-memory condition)
    default:
        // we should have a single value which is either a string (the error message) or kCancelError
        LUA_ASSERT(L_, (lua_gettop(L_) == stk_base_) && ((lua_type(L_, stk_base_) == LUA_TSTRING) || kCancelError.equals(L_, stk_base_)));
        break;
    }
}

// #################################################################################################
//---
// Run finalizers - if any - with the given parameters
//
// If 'rc' is nonzero, error message and stack index (the latter only when ERROR_FULL_STACK == 1) are available as:
//      [-1]: stack trace (table)
//      [-2]: error message (any type)
//
// Returns:
//      0 if finalizers were run without error (or there were none)
//      LUA_ERRxxx return code if any of the finalizers failed
//
// TBD: should we add stack trace on failing finalizer, wouldn't be hard..
//

[[nodiscard]] static int run_finalizers(lua_State* L_, int lua_rc_)
{
    kFinalizerRegKey.pushValue(L_);                                                                // L_: ... finalizers?
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);
        return 0; // no finalizers
    }

    STACK_GROW(L_, 5);

    int const _finalizers_index{ lua_gettop(L_) };
    int const _err_handler_index{ ERROR_FULL_STACK ? (lua_pushcfunction(L_, lane_error), lua_gettop(L_)) : 0 };

    int rc{ LUA_OK };
    for (int n = static_cast<int>(lua_rawlen(L_, _finalizers_index)); n > 0; --n) {
        int args = 0;
        lua_pushinteger(L_, n);                                                                    // L_: ... finalizers lane_error n
        lua_rawget(L_, _finalizers_index);                                                         // L_: ... finalizers lane_error finalizer
        LUA_ASSERT(L_, lua_isfunction(L_, -1));
        if (lua_rc_ != LUA_OK) { // we have an error message and an optional stack trace at the bottom of the stack
            LUA_ASSERT(L_, _finalizers_index == 2 || _finalizers_index == 3);
            // char const* err_msg = lua_tostring(L_, 1);
            lua_pushvalue(L_, 1);                                                                  // L_: ... finalizers lane_error finalizer err_msg
            // note we don't always have a stack trace for example when kCancelError, or when we got an error that doesn't call our handler, such as LUA_ERRMEM
            if (_finalizers_index == 3) {
                lua_pushvalue(L_, 2); // L_: ... finalizers lane_error finalizer err_msg stack_trace
            }
            args = _finalizers_index - 1;
        }

        // if no error from the main body, finalizer doesn't receive any argument, else it gets the error message and optional stack trace
        rc = lua_pcall(L_, args, 0, _err_handler_index);                                           // L_: ... finalizers lane_error err_msg2?
        if (rc != LUA_OK) {
            push_stack_trace(L_, rc, lua_gettop(L_));                                              // L_: ... finalizers lane_error err_msg2? trace
            // If one finalizer fails, don't run the others. Return this
            // as the 'real' error, replacing what we could have had (or not)
            // from the actual code.
            break;
        }
        // no error, proceed to next finalizer                                                     // L_: ... finalizers lane_error
    }

    if (rc != LUA_OK) {
        // ERROR_FULL_STACK accounts for the presence of lane_error on the stack
        int const nb_err_slots{ lua_gettop(L_) - _finalizers_index - ERROR_FULL_STACK };
        // a finalizer generated an error, this is what we leave of the stack
        for (int n = nb_err_slots; n > 0; --n) {
            lua_replace(L_, n);
        }
        // leave on the stack only the error and optional stack trace produced by the error in the finalizer
        lua_settop(L_, nb_err_slots);                                                              // L_: ... lane_error trace
    } else { // no error from the finalizers, make sure only the original return values from the lane body remain on the stack
        lua_settop(L_, _finalizers_index - 1);
    }

    return rc;
}

// #################################################################################################
// ########################################### Threads #############################################
// #################################################################################################

/*
 * Add the lane to selfdestruct chain; the ones still running at the end of the
 * whole process will be cancelled.
 */
static void selfdestruct_add(Lane* lane_)
{
    std::lock_guard<std::mutex> _guard{ lane_->U->selfdestructMutex };
    assert(lane_->selfdestruct_next == nullptr);

    lane_->selfdestruct_next = lane_->U->selfdestructFirst;
    lane_->U->selfdestructFirst = lane_;
}

// #################################################################################################

// A free-running lane has ended; remove it from selfdestruct chain
[[nodiscard]] static bool selfdestruct_remove(Lane* lane_)
{
    bool _found{ false };
    std::lock_guard<std::mutex> _guard{ lane_->U->selfdestructMutex };
    // Make sure (within the MUTEX) that we actually are in the chain
    // still (at process exit they will remove us from chain and then
    // cancel/kill).
    //
    if (lane_->selfdestruct_next != nullptr) {
        Lane* volatile* _ref = static_cast<Lane* volatile*>(&lane_->U->selfdestructFirst);

        while (*_ref != SELFDESTRUCT_END) {
            if (*_ref == lane_) {
                *_ref = lane_->selfdestruct_next;
                lane_->selfdestruct_next = nullptr;
                // the terminal shutdown should wait until the lane is done with its lua_close()
                lane_->U->selfdestructingCount.fetch_add(1, std::memory_order_release);
                _found = true;
                break;
            }
            _ref = static_cast<Lane* volatile*>(&((*_ref)->selfdestruct_next));
        }
        assert(_found);
    }
    return _found;
}

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

/*
 * str= lane_error( error_val|str )
 *
 * Called if there's an error in some lane; add call stack to error message
 * just like 'lua.c' normally does.
 *
 * ".. will be called with the error message and its return value will be the
 *     message returned on the stack by lua_pcall."
 *
 * Note: Rather than modifying the error message itself, it would be better
 *     to provide the call stack (as string) completely separated. This would
 *     work great with non-string error values as well (current system does not).
 *     (This is NOT possible with the Lua 5.1 'lua_pcall()'; we could of course
 *     implement a Lanes-specific 'pcall' of our own that does this). TBD!!! :)
 *       --AKa 22-Jan-2009
 */
#if ERROR_FULL_STACK

// xxh64 of string "kExtendedStackTraceRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kExtendedStackTraceRegKey{ 0x38147AD48FB426E2ull }; // used as registry key

LUAG_FUNC(set_error_reporting)
{
    luaL_checktype(L_, 1, LUA_TSTRING);
    char const* _mode{ lua_tostring(L_, 1) };
    lua_pushliteral(L_, "extended");
    bool const _extended{ strcmp(_mode, "extended") == 0 };
    bool const _basic{ strcmp(_mode, "basic") == 0 };
    if (!_extended && !_basic) {
        raise_luaL_error(L_, "unsupported error reporting model %s", _mode);
    }

    kExtendedStackTraceRegKey.setValue(L_, [extended = _extended](lua_State* L_) { lua_pushboolean(L_, extended ? 1 : 0); });
    return 0;
}

// #################################################################################################

[[nodiscard]] static int lane_error(lua_State* L_)
{
    // error message (any type)
    STACK_CHECK_START_ABS(L_, 1); // L_: some_error

    // Don't do stack survey for cancelled lanes.
    //
    if (kCancelError.equals(L_, 1)) {
        return 1; // just pass on
    }

    STACK_GROW(L_, 3);
    bool const _extended{ kExtendedStackTraceRegKey.readBoolValue(L_) };
    STACK_CHECK(L_, 1);

    // Place stack trace at 'registry[kStackTraceRegKey]' for the 'lua_pcall()'
    // caller to fetch. This bypasses the Lua 5.1 limitation of only one
    // return value from error handler to 'lua_pcall()' caller.

    // It's adequate to push stack trace as a table. This gives the receiver
    // of the stack best means to format it to their liking. Also, it allows
    // us to add more stack info later, if needed.
    //
    // table of { "sourcefile.lua:<line>", ... }
    //
    lua_newtable(L_);                                                                              // L_: some_error {}

    // Best to start from level 1, but in some cases it might be a C function
    // and we don't get '.currentline' for that. It's okay - just keep level
    // and table index growing separate.    --AKa 22-Jan-2009
    //
    lua_Debug _ar;
    for (int _n = 1; lua_getstack(L_, _n, &_ar); ++_n) {
        lua_getinfo(L_, _extended ? "Sln" : "Sl", &_ar);
        if (_extended) {
            lua_newtable(L_);                                                                      // L_: some_error {} {}

            lua_pushstring(L_, _ar.source);                                                        // L_: some_error {} {} source
            lua_setfield(L_, -2, "source");                                                        // L_: some_error {} {}

            lua_pushinteger(L_, _ar.currentline);                                                  // L_: some_error {} {} currentline
            lua_setfield(L_, -2, "currentline");                                                   // L_: some_error {} {}

            lua_pushstring(L_, _ar.name);                                                          // L_: some_error {} {} name
            lua_setfield(L_, -2, "name");                                                          // L_: some_error {} {}

            lua_pushstring(L_, _ar.namewhat);                                                      // L_: some_error {} {} namewhat
            lua_setfield(L_, -2, "namewhat");                                                      // L_: some_error {} {}

            lua_pushstring(L_, _ar.what);                                                          // L_: some_error {} {} what
            lua_setfield(L_, -2, "what");                                                          // L_: some_error {} {}
        } else if (_ar.currentline > 0) {
            lua_pushfstring(L_, "%s:%d", _ar.short_src, _ar.currentline);                          // L_: some_error {} "blah:blah"
        } else {
            lua_pushfstring(L_, "%s:?", _ar.short_src);                                            // L_: some_error {} "blah"
        }
        lua_rawseti(L_, -2, static_cast<lua_Integer>(_n));                                         // L_: some_error {}
    }

    // store the stack trace table in the registry
    kStackTraceRegKey.setValue(L_, [](lua_State* L_) { lua_insert(L_, -2); });                     // L_: some_error

    STACK_CHECK(L_, 1);
    return 1; // the untouched error value
}
#endif // ERROR_FULL_STACK

// #################################################################################################

void Lane::changeDebugName(int nameIdx_)
{
    // xxh64 of string "debugName" generated at https://www.pelock.com/products/hash-calculator
    static constexpr RegistryUniqueKey kRegKey{ 0xA194E2645C57F6DDull };
    nameIdx_ = lua_absindex(L, nameIdx_);
    luaL_checktype(L, nameIdx_, LUA_TSTRING);                                                      // L: ... "name" ...
    STACK_CHECK_START_REL(L, 0);
    // store a hidden reference in the registry to make sure the string is kept around even if a lane decides to manually change the "decoda_name" global...
    kRegKey.setValue(L, [nameIdx = nameIdx_](lua_State* L_) { lua_pushvalue(L_, nameIdx); });      // L: ... "name" ...
    // keep a direct pointer on the string
    debugName = lua_tostring(L, nameIdx_);
    // to see VM name in Decoda debugger Virtual Machine window
    lua_pushvalue(L, nameIdx_);                                                                    // L: ... "name" ... "name"
    lua_setglobal(L, "decoda_name");                                                               // L: ... "name" ...
    // and finally set the OS thread name
    THREAD_SETNAME(debugName);
    STACK_CHECK(L, 0);
}

// #################################################################################################

// upvalue #1 is the lane userdata
LUAG_FUNC(set_debug_threadname)
{
    // C s_lane structure is a light userdata upvalue
    Lane* const _lane{ lua_tolightuserdata<Lane>(L_, lua_upvalueindex(1)) };
    LUA_ASSERT(L_, L_ == _lane->L); // this function is exported in a lane's state, therefore it is callable only from inside the Lane's state
    lua_settop(L_, 1);
    STACK_CHECK_START_REL(L_, 0);
    _lane->changeDebugName(-1);
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

LUAG_FUNC(get_debug_threadname)
{
    Lane* const _lane{ ToLane(L_, 1) };
    luaL_argcheck(L_, lua_gettop(L_) == 1, 2, "too many arguments");
    lua_pushstring(L_, _lane->debugName);
    return 1;
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

#if USE_DEBUG_SPEW()
// can't use direct LUA_x errcode indexing because the sequence is not the same between Lua 5.1 and 5.2 :-(
// LUA_ERRERR doesn't have the same value
struct errcode_name
{
    int code;
    char const* name;
};

static struct errcode_name s_errcodes[] = {
    { LUA_OK, "LUA_OK" },
    { LUA_YIELD, "LUA_YIELD" },
    { LUA_ERRRUN, "LUA_ERRRUN" },
    { LUA_ERRSYNTAX, "LUA_ERRSYNTAX" },
    { LUA_ERRMEM, "LUA_ERRMEM" },
    { LUA_ERRGCMM, "LUA_ERRGCMM" },
    { LUA_ERRERR, "LUA_ERRERR" },
};
static char const* get_errcode_name(int _code)
{
    for (errcode_name const& _entry : s_errcodes) {
        if (_entry.code == _code) {
            return _entry.name;
        }
    }
    return "<nullptr>";
}
#endif // USE_DEBUG_SPEW()

// #################################################################################################

static void lane_main(Lane* lane_)
{
    lua_State* const _L{ lane_->L };
    // wait until the launching thread has finished preparing L
    lane_->ready.wait();
    int _rc{ LUA_ERRRUN };
    if (lane_->status == Lane::Pending) { // nothing wrong happened during preparation, we can work
        // At this point, the lane function and arguments are on the stack
        int const nargs{ lua_gettop(_L) - 1 };
        DEBUGSPEW_CODE(Universe* U = universe_get(_L));
        lane_->status = Lane::Running; // Pending -> Running

        // Tie "set_finalizer()" to the state
        lua_pushcfunction(_L, LG_set_finalizer);
        populate_func_lookup_table(_L, -1, "set_finalizer");
        lua_setglobal(_L, "set_finalizer");

        // Tie "set_debug_threadname()" to the state
        // But don't register it in the lookup database because of the Lane pointer upvalue
        lua_pushlightuserdata(_L, lane_);
        lua_pushcclosure(_L, LG_set_debug_threadname, 1);
        lua_setglobal(_L, "set_debug_threadname");

        // Tie "cancel_test()" to the state
        lua_pushcfunction(_L, LG_cancel_test);
        populate_func_lookup_table(_L, -1, "cancel_test");
        lua_setglobal(_L, "cancel_test");

        // this could be done in lane_new before the lane body function is pushed on the stack to avoid unnecessary stack slot shifting around
#if ERROR_FULL_STACK
        // Tie "set_error_reporting()" to the state
        lua_pushcfunction(_L, LG_set_error_reporting);
        populate_func_lookup_table(_L, -1, "set_error_reporting");
        lua_setglobal(_L, "set_error_reporting");

        STACK_GROW(_L, 1);
        lua_pushcfunction(_L, lane_error);                                                         // L: func args handler
        lua_insert(_L, 1);                                                                         // L: handler func args
#endif                                                                                             // L: ERROR_FULL_STACK

        _rc = lua_pcall(_L, nargs, LUA_MULTRET, ERROR_FULL_STACK);                                 // L: retvals|err

#if ERROR_FULL_STACK
        lua_remove(_L, 1);                                                                         // L: retvals|error
#endif // ERROR_FULL_STACK

        // in case of error and if it exists, fetch stack trace from registry and push it
        push_stack_trace(_L, _rc, 1);                                                              // L: retvals|error [trace]

        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "Lane %p body: %s (%s)\n" INDENT_END(U), _L, get_errcode_name(_rc), kCancelError.equals(_L, 1) ? "cancelled" : lua_typename(_L, lua_type(_L, 1))));
        //  Call finalizers, if the script has set them up.
        //
        int _rc2{ run_finalizers(_L, _rc) };
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "Lane %p finalizer: %s\n" INDENT_END(U), _L, get_errcode_name(_rc2)));
        if (_rc2 != LUA_OK) { // Error within a finalizer!
            // the finalizer generated an error, and left its own error message [and stack trace] on the stack
            _rc = _rc2; // we're overruling the earlier script error or normal return
        }
        lane_->waiting_on = nullptr;  // just in case
        if (selfdestruct_remove(lane_)) { // check and remove (under lock!)
            // We're a free-running thread and no-one's there to clean us up.
            lua_close(lane_->L);
            lane_->L = nullptr; // just in case
            lane_->U->selfdestructMutex.lock();
            // done with lua_close(), terminal shutdown sequence may proceed
            lane_->U->selfdestructingCount.fetch_sub(1, std::memory_order_release);
            lane_->U->selfdestructMutex.unlock();

            // we destroy our jthread member from inside the thread body, so we have to detach so that we don't try to join, as this doesn't seem a good idea
            lane_->thread.detach();
            delete lane_;
            lane_ = nullptr;
        }
    }
    if (lane_) {
        // leave results (1..top) or error message + stack trace (1..2) on the stack - master will copy them

        Lane::Status const _st = (_rc == LUA_OK) ? Lane::Done : kCancelError.equals(_L, 1) ? Lane::Cancelled : Lane::Error;

        {
            // 'doneMutex' protects the -> Done|Error|Cancelled state change
            std::lock_guard lock{ lane_->doneMutex };
            lane_->status = _st;
            lane_->doneCondVar.notify_one(); // wake up master (while 'lane_->doneMutex' is on)
        }
    }
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

// xxh64 of string "kLaneGC" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kLaneGC{ 0x5D6122141727F960ull };

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

// #################################################################################################

// = thread_gc( lane_ud )
//
// Cleanup for a thread userdata. If the thread is still executing, leave it
// alive as a free-running thread (will clean up itself).
//
// * Why NOT cancel/kill a loose thread: 
//
// At least timer system uses a free-running thread, they should be handy
// and the issue of canceling/killing threads at gc is not very nice, either
// (would easily cause waits at gc cycle, which we don't want).
//
[[nodiscard]] static int lane_gc(lua_State* L_)
{
    bool _have_gc_cb{ false };
    Lane* const _lane{ ToLane(L_, 1) };                                                             // L_: ud

    // if there a gc callback?
    lua_getiuservalue(L_, 1, 1);                                                                   // L_: ud uservalue
    kLaneGC.pushKey(L_);                                                                           // L_: ud uservalue __gc
    lua_rawget(L_, -2);                                                                            // L_: ud uservalue gc_cb|nil
    if (!lua_isnil(L_, -1)) {
        lua_remove(L_, -2);                                                                        // L_: ud gc_cb|nil
        lua_pushstring(L_, _lane->debugName);                                                      // L_: ud gc_cb name
        _have_gc_cb = true;
    } else {
        lua_pop(L_, 2);                                                                            // L_: ud
    }

    // We can read 'lane->status' without locks, but not wait for it
    if (_lane->status < Lane::Done) {
        // still running: will have to be cleaned up later
        selfdestruct_add(_lane);
        assert(_lane->selfdestruct_next);
        if (_have_gc_cb) {
            lua_pushliteral(L_, "selfdestruct");                                                   // L_: ud gc_cb name status
            lua_call(L_, 2, 0);                                                                    // L_: ud
        }
        return 0;
    } else if (_lane->L) {
        // no longer accessing the Lua VM: we can close right now
        lua_close(_lane->L);
        _lane->L = nullptr;
        // just in case, but s will be freed soon so...
        _lane->debugName = "<gc>";
    }

    // Clean up after a (finished) thread
    delete _lane;

    // do this after lane cleanup in case the callback triggers an error
    if (_have_gc_cb) {
        lua_pushliteral(L_, "closed");                                                             // L_: ud gc_cb name status
        lua_call(L_, 2, 0);                                                                        // L_: ud
    }
    return 0;
}

// #################################################################################################

//---
// str= thread_status( lane )
//
// Returns: "pending"   not started yet
//          -> "running"   started, doing its work..
//             <-> "waiting"   blocked in a receive()
//                -> "done"     finished, results are there
//                   / "error"     finished at an error, error value is there
//                   / "cancelled"   execution cancelled by M (state gone)
//
[[nodiscard]] static char const* thread_status_string(Lane::Status status_)
{
    char const* const _str{
        (status_ == Lane::Pending) ? "pending" :
        (status_ == Lane::Running) ? "running" :    // like in 'co.status()'
        (status_ == Lane::Waiting) ? "waiting" :
        (status_ == Lane::Done) ? "done" :
        (status_ == Lane::Error) ? "error" :
        (status_ == Lane::Cancelled) ? "cancelled" :
        nullptr
    };
    return _str;
}

// #################################################################################################

void Lane::pushThreadStatus(lua_State* L_)
{
    char const* const _str{ thread_status_string(status) };
    LUA_ASSERT(L_, _str);

    lua_pushstring(L_, _str);
}

// #################################################################################################

//---
// [...] | [nil, err_any, stack_tbl]= thread_join( lane_ud [, wait_secs=-1] )
//
//  timeout:   returns nil
//  done:      returns return values (0..N)
//  error:     returns nil + error value [+ stack table]
//  cancelled: returns nil
//
LUAG_FUNC(thread_join)
{
    Lane* const _lane{ ToLane(L_, 1) };
    lua_State* const _L2{ _lane->L };

    std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
    if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
        lua_Duration const duration{ lua_tonumber(L_, 2) };
        if (duration.count() >= 0.0) {
            _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        } else {
            raise_luaL_argerror(L_, 2, "duration cannot be < 0");
        }

    } else if (!lua_isnoneornil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
        raise_luaL_argerror(L_, 2, "incorrect duration type");
    }

    bool const done{ !_lane->thread.joinable() || _lane->waitForCompletion(_until) };
    lua_settop(L_, 1);                                                                             // L_: lane
    if (!done || !_L2) {
        lua_pushnil(L_);                                                                           // L_: lane nil
        lua_pushliteral(L_, "timeout");                                                            // L_: lane nil "timeout"
        return 2;
    }

    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: lane
    // Thread is Done/Error/Cancelled; all ours now

    int _ret{ 0 };
    // debugName is a pointer to string possibly interned in the lane's state, that no longer exists when the state is closed
    // so store it in the userdata uservalue at a key that can't possibly collide
    _lane->securizeDebugName(L_);
    switch (_lane->status) {
    case Lane::Done:
        {
            int const _n{ lua_gettop(_L2) }; // whole L2 stack
            if (
                (_n > 0) &&
                (InterCopyContext{ _lane->U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} }.inter_move(_n) != InterCopyResult::Success)
            ) {                                                                                    // L_: lane results                                L2:
                raise_luaL_error(L_, "tried to copy unsupported types");
            }
            _ret = _n;
        }
        break;

    case Lane::Error:
        {
            int const _n{ lua_gettop(_L2) };                                                       // L_: lane                                        L2: "err" [trace]
            STACK_GROW(L_, 3);
            lua_pushnil(L_);                                                                       // L_: lane nil
            // even when ERROR_FULL_STACK, if the error is not LUA_ERRRUN, the handler wasn't called, and we only have 1 error message on the stack ...
            InterCopyContext _c{ _lane->U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} };
            if (_c.inter_move(_n) != InterCopyResult::Success) {                                     // L_: lane nil "err" [trace]                      L2:
                raise_luaL_error(L_, "tried to copy unsupported types: %s", lua_tostring(L_, -_n));
            }
            _ret = 1 + _n;
        }
        break;

    case Lane::Cancelled:
        _ret = 0;
        break;

    default:
        DEBUGSPEW_CODE(fprintf(stderr, "Status: %d\n", _lane->status));
        LUA_ASSERT(L_, false);
        _ret = 0;
    }
    lua_close(_L2);
    _lane->L = nullptr;
    STACK_CHECK(L_, _ret);
    return _ret;
}

// #################################################################################################

// lane:__index(key,usr) -> value
//
// If key is found in the environment, return it
// If key is numeric, wait until the thread returns and populate the environment with the return values
// If the return values signal an error, propagate it
// If key is "status" return the thread status
// Else raise an error
LUAG_FUNC(thread_index)
{
    static constexpr int kSelf{ 1 };
    static constexpr int kKey{ 2 };
    Lane* const _lane{ ToLane(L_, kSelf) };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);

    STACK_GROW(L_, 8); // up to 8 positions are needed in case of error propagation

    // If key is numeric, wait until the thread returns and populate the environment with the return values
    if (lua_type(L_, kKey) == LUA_TNUMBER) {
        static constexpr int kUsr{ 3 };
        // first, check that we don't already have an environment that holds the requested value
        {
            // If key is found in the uservalue, return it
            lua_getiuservalue(L_, kSelf, 1);
            lua_pushvalue(L_, kKey);
            lua_rawget(L_, kUsr);
            if (!lua_isnil(L_, -1)) {
                return 1;
            }
            lua_pop(L_, 1);
        }
        {
            // check if we already fetched the values from the thread or not
            lua_pushinteger(L_, 0);
            lua_rawget(L_, kUsr);
            bool const _fetched{ !lua_isnil(L_, -1) };
            lua_pop(L_, 1); // back to our 2 args + uservalue on the stack
            if (!_fetched) {
                lua_pushinteger(L_, 0);
                lua_pushboolean(L_, 1);
                lua_rawset(L_, kUsr);
                // wait until thread has completed
                lua_pushcfunction(L_, LG_thread_join);
                lua_pushvalue(L_, kSelf);
                lua_call(L_, 1, LUA_MULTRET); // all return values are on the stack, at slots 4+
                switch (_lane->status) {
                default:
                    // this is an internal error, we probably never get here
                    lua_settop(L_, 0);
                    lua_pushliteral(L_, "Unexpected status: ");
                    lua_pushstring(L_, thread_status_string(_lane->status));
                    lua_concat(L_, 2);
                    raise_lua_error(L_);
                    [[fallthrough]]; // fall through if we are killed, as we got nil, "killed" on the stack

                case Lane::Done: // got regular return values
                    {
                        int const _nvalues{ lua_gettop(L_) - 3 };
                        for (int _i = _nvalues; _i > 0; --_i) {
                            // pop the last element of the stack, to store it in the uservalue at its proper index
                            lua_rawseti(L_, kUsr, _i);
                        }
                    }
                    break;

                case Lane::Error: // got 3 values: nil, errstring, callstack table
                    // me[-2] could carry the stack table, but even
                    // me[-1] is rather unnecessary (and undocumented);
                    // use ':join()' instead.   --AKa 22-Jan-2009
                    LUA_ASSERT(L_, lua_isnil(L_, 4) && !lua_isnil(L_, 5) && lua_istable(L_, 6));
                    // store errstring at key -1
                    lua_pushnumber(L_, -1);
                    lua_pushvalue(L_, 5);
                    lua_rawset(L_, kUsr);
                    break;

                case Lane::Cancelled:
                    // do nothing
                    break;
                }
            }
            lua_settop(L_, 3);                                                                     // L_: self KEY ENV
            int const _key{ static_cast<int>(lua_tointeger(L_, kKey)) };
            if (_key != -1) {
                lua_pushnumber(L_, -1);                                                            // L_: self KEY ENV -1
                lua_rawget(L_, kUsr);                                                              // L_: self KEY ENV "error"|nil
                if (!lua_isnil(L_, -1)) {                                                          // L_: an error was stored
                    // Note: Lua 5.1 interpreter is not prepared to show
                    //       non-string errors, so we use 'tostring()' here
                    //       to get meaningful output.  --AKa 22-Jan-2009
                    //
                    //       Also, the stack dump we get is no good; it only
                    //       lists our internal Lanes functions. There seems
                    //       to be no way to switch it off, though.
                    //
                    // Level 3 should show the line where 'h[x]' was read
                    // but this only seems to work for string messages
                    // (Lua 5.1.4). No idea, why.   --AKa 22-Jan-2009
                    lua_getmetatable(L_, kSelf);                                                   // L_: self KEY ENV "error" mt
                    lua_getfield(L_, -1, "cached_error");                                          // L_: self KEY ENV "error" mt error()
                    lua_getfield(L_, -2, "cached_tostring");                                       // L_: self KEY ENV "error" mt error() tostring()
                    lua_pushvalue(L_, 4);                                                          // L_: self KEY ENV "error" mt error() tostring() "error"
                    lua_call(L_, 1, 1); // tostring(errstring) -- just in case                     // L_: self KEY ENV "error" mt error() "error"
                    lua_pushinteger(L_, 3);                                                        // L_: self KEY ENV "error" mt error() "error" 3
                    lua_call(L_, 2, 0); // error(tostring(errstring), 3) -> doesn't return         // L_: self KEY ENV "error" mt
                } else {
                    lua_pop(L_, 1);                                                                // L_: self KEY ENV
                }
            }
            lua_rawgeti(L_, kUsr, _key);
        }
        return 1;
    }
    if (lua_type(L_, kKey) == LUA_TSTRING) {
        char const* const _keystr{ lua_tostring(L_, kKey) };
        lua_settop(L_, 2); // keep only our original arguments on the stack
        if (strcmp(_keystr, "status") == 0) {
            _lane->pushThreadStatus(L_); // push the string representing the status
            return 1;
        }
        // return self.metatable[key]
        lua_getmetatable(L_, kSelf);                                                               // L_: self KEY mt
        lua_replace(L_, -3);                                                                       // L_: mt KEY
        lua_rawget(L_, -2);                                                                        // L_: mt value
        // only "cancel" and "join" are registered as functions, any other string will raise an error
        if (!lua_iscfunction(L_, -1)) {
            raise_luaL_error(L_, "can't index a lane with '%s'", _keystr);
        }
        return 1;
    }
    // unknown key
    lua_getmetatable(L_, kSelf);                                                                   // L_: mt
    lua_getfield(L_, -1, "cached_error");                                                          // L_: mt error
    lua_pushliteral(L_, "Unknown key: ");                                                          // L_: mt error "Unknown key: "
    lua_pushvalue(L_, kKey);                                                                       // L_: mt error "Unknown key: " k
    lua_concat(L_, 2);                                                                             // L_: mt error "Unknown key: <k>"
    lua_call(L_, 1, 0); // error( "Unknown key: " .. key) -> doesn't return                        // L_: mt
    return 0;
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
    //
    if (luaL_newmetatable(L_, kLaneMetatableName)) {                                               // L_: settings M mt
        lua_pushcfunction(L_, lane_gc);                                                            // L_: settings M mt lane_gc
        lua_setfield(L_, -2, "__gc");                                                              // L_: settings M mt
        lua_pushcfunction(L_, LG_thread_index);                                                    // L_: settings M mt LG_thread_index
        lua_setfield(L_, -2, "__index");                                                           // L_: settings M mt
        lua_getglobal(L_, "error");                                                                // L_: settings M mt error
        LUA_ASSERT(L_, lua_isfunction(L_, -1));
        lua_setfield(L_, -2, "cached_error");                                                      // L_: settings M mt
        lua_getglobal(L_, "tostring");                                                             // L_: settings M mt tostring
        LUA_ASSERT(L_, lua_isfunction(L_, -1));
        lua_setfield(L_, -2, "cached_tostring");                                                   // L_: settings M mt
        lua_pushcfunction(L_, LG_thread_join);                                                     // L_: settings M mt LG_thread_join
        lua_setfield(L_, -2, "join");                                                              // L_: settings M mt
        lua_pushcfunction(L_, LG_get_debug_threadname);                                            // L_: settings M mt LG_get_debug_threadname
        lua_setfield(L_, -2, "get_debug_threadname");                                              // L_: settings M mt
        lua_pushcfunction(L_, LG_thread_cancel);                                                   // L_: settings M mt LG_thread_cancel
        lua_setfield(L_, -2, "cancel");                                                            // L_: settings M mt
        lua_pushliteral(L_, kLaneMetatableName);                                                   // L_: settings M mt "Lane"
        lua_setfield(L_, -2, "__metatable");                                                       // L_: settings M mt
    }

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
