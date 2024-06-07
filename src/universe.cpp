/*
 * UNIVERSE.CPP                  Copyright (c) 2017-2024, Benoit Germain
 */

/*
===============================================================================

Copyright (C) 2017-2024 Benoit Germain <bnt.germain@gmail.com>

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
#include "universe.h"

#include "deep.h"
#include "intercopycontext.h"
#include "keeper.h"
#include "lane.h"
#include "state.h"

extern LUAG_FUNC(linda);

// #################################################################################################

// xxh64 of string "kUniverseFullRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseFullRegKey{ 0x1C2D76870DD9DD9Full };

// #################################################################################################

Universe::Universe()
{
    //---
    // Linux needs SCHED_RR to change thread priorities, and that is only
    // allowed for sudo'ers. SCHED_OTHER (default) has no priorities.
    // SCHED_OTHER threads are always lower priority than SCHED_RR.
    //
    // ^-- those apply to 2.6 kernel.  IF **wishful thinking** these
    //     constraints will change in the future, non-sudo priorities can
    //     be enabled also for Linux.
    //
#ifdef PLATFORM_LINUX
    // If lower priorities (-2..-1) are wanted, we need to lift the main
    // thread to SCHED_RR and 50 (medium) level. Otherwise, we're always below
    // the launched threads (even -2).
    //
#ifdef LINUX_SCHED_RR
    if (sudo) {
        struct sched_param sp;
        sp.sched_priority = _PRIO_0;
        PT_CALL(pthread_setschedparam(pthread_self(), SCHED_RR, &sp));
    }
#endif // LINUX_SCHED_RR
#endif // PLATFORM_LINUX
}

// #################################################################################################

// only called from the master state
[[nodiscard]] Universe* Universe::Create(lua_State* const L_)
{
    LUA_ASSERT(L_, Universe::Get(L_) == nullptr);
    LUA_ASSERT(L_, lua_gettop(L_) == 1 && lua_istable(L_, 1));
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: settings
    std::ignore = luaG_getfield(L_, 1, "nb_user_keepers");                                         // L_: settings nb_user_keepers
    int const _nbUserKeepers{ static_cast<int>(lua_tointeger(L_, -1)) + 1};
    lua_pop(L_, 1);                                                                                // L_: settings
    if (_nbUserKeepers < 1) {
        raise_luaL_error(L_, "Bad number of additional keepers (%d)", _nbUserKeepers);
    }
    STACK_CHECK(L_, 0);
    std::ignore = luaG_getfield(L_, 1, "keepers_gc_threshold");                                    // L_: settings keepers_gc_threshold
    int const _keepers_gc_threshold{ static_cast<int>(lua_tointeger(L_, -1)) };
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 0);

    Universe* const _U{ new (L_) Universe{} };                                                     // L_: settings universe
    STACK_CHECK(L_, 1);
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    kUniverseLightRegKey.setValue(L_, [U = _U](lua_State* L_) { lua_pushlightuserdata(L_, U); });
    STACK_CHECK(L_, 1);                                                                            // L_: settings

    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
    lua_createtable(L_, 0, 1);                                                                     // L_: settings universe {mt}
    std::ignore = luaG_getfield(L_, 1, "shutdown_timeout");                                        // L_: settings universe {mt} shutdown_timeout
    std::ignore = luaG_getfield(L_, 1, "shutdown_mode");                                           // L_: settings universe {mt} shutdown_timeout shutdown_mode
    lua_pushcclosure(L_, LG_universe_gc, 2);                                                       // L_: settings universe {mt} LG_universe_gc
    lua_setfield(L_, -2, "__gc");                                                                  // L_: settings universe {mt}
    lua_setmetatable(L_, -2);                                                                      // L_: settings universe
    lua_pop(L_, 1);                                                                                // L_: settings

    // TODO: write some tests to see what happens when we trigger errors in stripped mode
    std::ignore = luaG_getfield(L_, 1, "strip_functions");                                         // L_: settings strip_functions
    _U->stripFunctions = lua_toboolean(L_, -1) ? true : false;
    lua_pop(L_, 1);                                                                                // L_: settings

    std::ignore = luaG_getfield(L_, 1, "verbose_errors");                                          // L_: settings verbose_errors
    _U->verboseErrors = lua_toboolean(L_, -1) ? true : false;
    lua_pop(L_, 1);                                                                                // L_: settings
    std::ignore = luaG_getfield(L_, 1, "demote_full_userdata");                                    // L_: settings demote_full_userdata
    _U->demoteFullUserdata = lua_toboolean(L_, -1) ? true : false;
    lua_pop(L_, 1);                                                                                // L_: settings

    // tracking
    std::ignore = luaG_getfield(L_, 1, "track_lanes");                                             // L_: settings track_lanes
    if (lua_toboolean(L_, -1)) {
        _U->tracker.activate();
    }
    lua_pop(L_, 1);                                                                                // L_: settings

    // Linked chains handling
    _U->selfdestructFirst = SELFDESTRUCT_END;
    _U->initializeAllocatorFunction(L_);
    state::InitializeOnStateCreate(_U, L_);
    _U->keepers.initialize(*_U, L_, _nbUserKeepers, _keepers_gc_threshold);
    STACK_CHECK(L_, 0);

    // Initialize 'timerLinda'; a common Linda object shared by all states
    lua_pushcfunction(L_, LG_linda);                                                               // L_: settings lanes.linda
    std::ignore = luaG_pushstringview(L_, "lanes-timer");                                          // L_: settings lanes.linda "lanes-timer"
    lua_pushinteger(L_, 0);                                                                        // L_: settings lanes.linda "lanes-timer" 0
    lua_call(L_, 2, 1);                                                                            // L_: settings linda
    STACK_CHECK(L_, 1);

    // Proxy userdata contents is only a 'DeepPrelude*' pointer
    _U->timerLinda = *lua_tofulluserdata<DeepPrelude*>(L_, -1);
    // increment refcount so that this linda remains alive as long as the universe exists.
    _U->timerLinda->refcount.fetch_add(1, std::memory_order_relaxed);
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 0);
    return _U;
}

// #################################################################################################
// ################################### custom allocator support ####################################
// #################################################################################################

// same as PUC-Lua l_alloc
[[nodiscard]] static void* libc_lua_Alloc([[maybe_unused]] void* ud_, [[maybe_unused]] void* ptr_, [[maybe_unused]] size_t osize_, size_t nsize_)
{
    if (nsize_ == 0) {
        free(ptr_);
        return nullptr;
    } else {
        return realloc(ptr_, nsize_);
    }
}

// #################################################################################################

[[nodiscard]] static int luaG_provide_protected_allocator(lua_State* const L_)
{
    Universe* const _U{ Universe::Get(L_) };
    // push a new full userdata on the stack, giving access to the universe's protected allocator
    [[maybe_unused]] AllocatorDefinition* const _def{ new (L_) AllocatorDefinition{ _U->protectedAllocator.makeDefinition() } };
    return 1;
}

// #################################################################################################

// called once at the creation of the universe (therefore L_ is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
void Universe::initializeAllocatorFunction(lua_State* const L_)
{
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    if (luaG_getfield(L_, -1, "allocator") != LuaType::NIL) {                                      // L_: settings allocator|nil|"protected"
        // store C function pointer in an internal variable
        provideAllocator = lua_tocfunction(L_, -1);                                                // L_: settings allocator
        if (provideAllocator != nullptr) {
            // make sure the function doesn't have upvalues
            char const* _upname = lua_getupvalue(L_, -1, 1);                                       // L_: settings allocator upval?
            if (_upname != nullptr) {   // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "config.allocator() shouldn't have upvalues");
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings allocator nil
            lua_setfield(L_, -3, "allocator");                                                     // L_: settings allocator
        } else if (luaG_type(L_, -1) == LuaType::STRING) { // should be "protected"
            LUA_ASSERT(L_, strcmp(lua_tostring(L_, -1), "protected") == 0);
            // set the original allocator to call from inside protection by the mutex
            protectedAllocator.initFrom(L_);
            protectedAllocator.installIn(L_);
            // before a state is created, this function will be called to obtain the allocator
            provideAllocator = luaG_provide_protected_allocator;
        }
    } else {
        // just grab whatever allocator was provided to lua_newstate
        protectedAllocator.initFrom(L_);
    }
    lua_pop(L_, 1); // L_: settings
    STACK_CHECK(L_, 1);

    std::ignore = luaG_getfield(L_, -1, "internal_allocator");                                     // L_: settings "libc"|"allocator"
    std::string_view const _allocator{ luaG_tostringview(L_, -1) };
    if (_allocator == "libc") {
        internalAllocator = AllocatorDefinition{ libc_lua_Alloc, nullptr };
    } else if (provideAllocator == luaG_provide_protected_allocator) {
        // user wants mutex protection on the state's allocator. Use protection for our own allocations too, just in case.
        internalAllocator = protectedAllocator.makeDefinition();
    } else {
        // no protection required, just use whatever we have as-is.
        internalAllocator = protectedAllocator;
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);
}

// #################################################################################################

// should be called ONLY from the state that created the universe
int Universe::InitializeFinalizer(lua_State* const L_)
{
    luaL_argcheck(L_, lua_gettop(L_) <= 1, 1, "too many arguments");                               // L_: f?
    lua_settop(L_, 1);                                                                             // L_: f|nil
    luaL_argcheck(L_, lua_isnoneornil(L_, 1) || lua_isfunction(L_, 1), 1, "finalizer should be a function");

    // make sure we are only called from the Master Lua State!
    kUniverseFullRegKey.pushValue(L_);                                                             // L_: f U
    if (luaG_type(L_, -1) != LuaType::USERDATA) {
        raise_luaL_error(L_, "lanes.%s called from inside a lane", kFinally);
    }
    lua_pop(L_, 1);                                                                                // L_: f
    STACK_GROW(L_, 3);
    // _R[kFinalizerRegKey] = f
    kFinalizerRegKey.setValue(L_, [](lua_State* L_) { lua_insert(L_, -2); });                      // L_:
    // no need to adjust the stack, Lua does this for us
    return 0;
}

// #################################################################################################

void Universe::terminateFreeRunningLanes(lua_State* const L_, lua_Duration const shutdownTimeout_, CancelOp const op_)
{
    if (selfdestructFirst != SELFDESTRUCT_END) {
        // Signal _all_ still running threads to exit (including the timer thread)
        {
            std::lock_guard<std::mutex> _guard{ selfdestructMutex };
            Lane* _lane{ selfdestructFirst };
            while (_lane != SELFDESTRUCT_END) {
                // attempt the requested cancel with a small timeout.
                // if waiting on a linda, they will raise a cancel_error.
                // if a cancellation hook is desired, it will be installed to try to raise an error
                if (_lane->thread.joinable()) {
                    std::ignore = thread_cancel(_lane, op_, 1, std::chrono::steady_clock::now() + 1us, true);
                }
                _lane = _lane->selfdestruct_next;
            }
        }

        // When noticing their cancel, the lanes will remove themselves from the selfdestruct chain.
        {
            std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(shutdownTimeout_) };

            while (selfdestructFirst != SELFDESTRUCT_END) {
                // give threads time to act on their cancel
                std::this_thread::yield();
                // count the number of cancelled thread that didn't have the time to act yet
                int _n{ 0 };
                {
                    std::lock_guard<std::mutex> _guard{ selfdestructMutex };
                    Lane* _lane{ selfdestructFirst };
                    while (_lane != SELFDESTRUCT_END) {
                        if (_lane->cancelRequest != CancelRequest::None)
                            ++_n;
                        _lane = _lane->selfdestruct_next;
                    }
                }
                // if timeout elapsed, or we know all threads have acted, stop waiting
                std::chrono::time_point<std::chrono::steady_clock> _now = std::chrono::steady_clock::now();
                if (_n == 0 || (_now >= _until)) {
                    DEBUGSPEW_CODE(DebugSpew(this) << _n << " uncancelled lane(s) remain after waiting " << shutdownTimeout_.count() << "s at process end." << std::endl);
                    break;
                }
            }
        }

        // If some lanes are currently cleaning after themselves, wait until they are done.
        // They are no longer listed in the selfdestruct chain, but they still have to lua_close().
        while (selfdestructingCount.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }

    // If after all this, we still have some free-running lanes, it's an external user error, they should have stopped appropriately
    {
        std::lock_guard<std::mutex> _guard{ selfdestructMutex };
        Lane* _lane{ selfdestructFirst };
        if (_lane != SELFDESTRUCT_END) {
            // this causes a leak because we don't call U's destructor (which could be bad if the still running lanes are accessing it)
            raise_luaL_error(L_, "Zombie thread '%s' refuses to die!", _lane->debugName.data());
        }
    }
}

// #################################################################################################

// process end: cancel any still free-running threads
LUAG_FUNC(universe_gc)
{
    lua_Duration const _shutdown_timeout{ lua_tonumber(L_, lua_upvalueindex(1)) };
    std::string_view const _op_string{ luaG_tostringview(L_, lua_upvalueindex(2)) };
    STACK_CHECK_START_ABS(L_, 1);
    Universe* const _U{ lua_tofulluserdata<Universe>(L_, 1) };                                     // L_: U
    _U->terminateFreeRunningLanes(L_, _shutdown_timeout, which_cancel_op(_op_string));

    // invoke the function installed by lanes.finally()
    kFinalizerRegKey.pushValue(L_);                                                                // L_: U finalizer|nil
    if (!lua_isnil(L_, -1)) {
        lua_pcall(L_, 0, 0, 0);                                                                    // L_: U
        // discard any error that might have occured
        lua_settop(L_, 1);
    } else {
        lua_pop(L_, 1);                                                                            // L_: U
    }
    // in case of error, the message is pushed on the stack
    STACK_CHECK(L_, 1);

    // no need to mutex-protect this as all threads in the universe are gone at that point
    if (_U->timerLinda != nullptr) { // test in case some early internal error prevented Lanes from creating the deep timer
        [[maybe_unused]] int const _prev_ref_count{ _U->timerLinda->refcount.fetch_sub(1, std::memory_order_relaxed) };
        LUA_ASSERT(L_, _prev_ref_count == 1); // this should be the last reference
        DeepFactory::DeleteDeepObject(L_, _U->timerLinda);
        _U->timerLinda = nullptr;
    }

    _U->keepers.close();

    // remove the protected allocator, if any
    _U->protectedAllocator.removeFrom(L_);

    // no longer found in the registry
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushnil(L_); });
    kUniverseLightRegKey.setValue(L_, [](lua_State* L_) { lua_pushnil(L_); });
    _U->Universe::~Universe();

    return 0;
}
