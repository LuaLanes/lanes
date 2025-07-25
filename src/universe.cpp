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

#include "_pch.hpp"
#include "universe.hpp"

#include "deep.hpp"
#include "intercopycontext.hpp"
#include "keeper.hpp"
#include "lane.hpp"
#include "linda.hpp"
#include "state.hpp"

extern LUAG_FUNC(linda);

// #################################################################################################

static constexpr std::string_view kOnStateCreate{ "on_state_create" }; // update lanes.lua if the name changes!

// #################################################################################################

// xxh64 of string "kUniverseFullRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseFullRegKey{ 0x1C2D76870DD9DD9Full };

// #################################################################################################

[[nodiscard]]
void* ProtectedAllocator::Protected_lua_Alloc(void* const ud_, void* const ptr_, size_t const osize_, size_t const nsize_)
{
    ProtectedAllocator* const _allocator{ static_cast<ProtectedAllocator*>(ud_) };
    std::lock_guard<std::mutex> _guard{ _allocator->mutex };
    return _allocator->alloc(ptr_, osize_, nsize_);
}

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

void Universe::callOnStateCreate(lua_State* const L_, lua_State* const from_, LookupMode const mode_)
{
    if (std::holds_alternative<std::nullptr_t>(onStateCreateFunc)) {
        return;
    }

    STACK_CHECK_START_REL(L_, 0);
    DEBUGSPEW_CODE(DebugSpew(this) << "calling on_state_create()" << std::endl);
    if (std::holds_alternative<lua_CFunction>(onStateCreateFunc)) {
            
        // C function: recreate a closure in the new state, bypassing the lookup scheme
        lua_pushcfunction(L_, std::get<lua_CFunction>(onStateCreateFunc));                         // on_state_create()
    } else { // Lua function located in the config table, copied when we opened "lanes_core"
        LUA_ASSERT(from_, std::holds_alternative<uintptr_t>(onStateCreateFunc));
        if (mode_ != LookupMode::LaneBody) {
            // if attempting to call in a keeper state, do nothing because the function doesn't exist there
            // this doesn't count as an error though
            STACK_CHECK(L_, 0);
            return;
        }
        kConfigRegKey.pushValue(L_);                                                               // L_: config
        STACK_CHECK(L_, 1);
        LuaType const _funcType{ luaW_getfield(L_, kIdxTop, kOnStateCreate) };                     // L_: config on_state_create()
        if (_funcType != LuaType::FUNCTION) {
            raise_luaL_error(L_, "INTERNAL ERROR: %s is a %s, not a function", kOnStateCreate.data(), luaW_typename(L_, _funcType).data());
        }
        lua_remove(L_, -2);                                                                        // L_: on_state_create()
    }
    STACK_CHECK(L_, 1);
    // capture error and raise it in caller state
    std::string_view const _stateType{ mode_ == LookupMode::LaneBody ? "lane" : "keeper" };
    luaW_pushstring(L_, _stateType);                                                               // L_: on_state_create() "<type>"
    if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
        raise_luaL_error(from_, "%s failed in %s: \"%s\"", kOnStateCreate.data(), _stateType.data(), lua_isstring(L_, -1) ? luaW_tostring(L_, kIdxTop).data() : luaW_typename(L_, kIdxTop).data());
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

// only called from the master state
[[nodiscard]]
Universe* Universe::Create(lua_State* const L_)
{
    LUA_ASSERT(L_, Universe::Get(L_) == nullptr);
    static constexpr StackIndex kIdxSettings{ 1 };
    LUA_ASSERT(L_, lua_gettop(L_) == 1 && lua_istable(L_, 1));
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: settings
    std::ignore = luaW_getfield(L_, kIdxSettings, "nb_user_keepers");                              // L_: settings nb_user_keepers
    int const _nbUserKeepers{ static_cast<int>(lua_tointeger(L_, -1)) + 1};
    lua_pop(L_, 1);                                                                                // L_: settings
    if (_nbUserKeepers < 1) {
        raise_luaL_error(L_, "Bad number of additional keepers (%d)", _nbUserKeepers);
    }
    STACK_CHECK(L_, 0);
    std::ignore = luaW_getfield(L_, kIdxSettings, "keepers_gc_threshold");                         // L_: settings keepers_gc_threshold
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
    std::ignore = luaW_getfield(L_, kIdxSettings, "shutdown_timeout");                             // L_: settings universe {mt} shutdown_timeout
    lua_pushcclosure(L_, UniverseGC, 1);                                                           // L_: settings universe {mt} UniverseGC
    lua_setfield(L_, -2, "__gc");                                                                  // L_: settings universe {mt}
    lua_setmetatable(L_, -2);                                                                      // L_: settings universe
    lua_pop(L_, 1);                                                                                // L_: settings

    std::ignore = luaW_getfield(L_, kIdxSettings, "linda_wake_period");                            // L_: settings linda_wake_period
    if (luaW_type(L_, kIdxTop) == LuaType::NUMBER) {
        _U->lindaWakePeriod = lua_Duration{ lua_tonumber(L_, kIdxTop) };
    } else {
        LUA_ASSERT(L_, luaW_tostring(L_, kIdxTop) == "never");
    }
    lua_pop(L_, 1);                                                                                // L_: settings

    std::ignore = luaW_getfield(L_, kIdxSettings, "strip_functions");                              // L_: settings strip_functions
    _U->stripFunctions = lua_toboolean(L_, -1) ? true : false;
    lua_pop(L_, 1);                                                                                // L_: settings

    std::ignore = luaW_getfield(L_, kIdxSettings, "verbose_errors");                               // L_: settings verbose_errors
    _U->verboseErrors = lua_toboolean(L_, -1) ? true : false;
    lua_pop(L_, 1);                                                                                // L_: settings

    // tracking
    std::ignore = luaW_getfield(L_, kIdxSettings, "track_lanes");                                  // L_: settings track_lanes
    if (lua_toboolean(L_, -1)) {
        _U->tracker.activate();
    }
    lua_pop(L_, 1);                                                                                // L_: settings

    // Linked chains handling
    _U->selfdestructFirst = SELFDESTRUCT_END;
    _U->initializeAllocatorFunction(L_); // this can raise an error
    _U->initializeOnStateCreate(L_); // this can raise an error
    _U->keepers.initialize(*_U, L_, static_cast<size_t>(_nbUserKeepers), _keepers_gc_threshold);
    STACK_CHECK(L_, 0);

    // Initialize 'timerLinda'; a common Linda object shared by all states
    _U->timerLinda = Linda::CreateTimerLinda(L_, PK);
    return _U;
}

// #################################################################################################
// ################################### custom allocator support ####################################
// #################################################################################################

// same as PUC-Lua l_alloc
[[nodiscard]]
static void* libc_lua_Alloc([[maybe_unused]] void* const ud_, [[maybe_unused]] void* const ptr_, [[maybe_unused]] size_t const osize_, size_t const nsize_)
{
    if (nsize_ == 0) {
        free(ptr_);
        return nullptr;
    } else {
        return realloc(ptr_, nsize_);
    }
}

// #################################################################################################

[[nodiscard]]
static int luaW_provide_protected_allocator(lua_State* const L_)
{
    Universe* const _U{ Universe::Get(L_) };
    // push a new full userdata on the stack, giving access to the universe's protected allocator
    [[maybe_unused]] lanes::AllocatorDefinition* const _def{ new (L_) lanes::AllocatorDefinition{ _U->protectedAllocator.makeDefinition() } };
    return 1;
}

// #################################################################################################

// already called under protection of selfdestructMutex
void Universe::flagDanglingLanes() const
{
    Lane* _lane{ selfdestructFirst };
    while (_lane != SELFDESTRUCT_END) {
        _lane->flaggedAfterUniverseGC.store(true, std::memory_order_relaxed);
        _lane = _lane->selfdestruct_next;
    }
}
// #################################################################################################

// called once at the creation of the universe (therefore L_ is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
void Universe::initializeAllocatorFunction(lua_State* const L_)
{
    // start by just grabbing whatever allocator was provided to the master state
    protectedAllocator.initFrom(L_);
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    switch (luaW_getfield(L_, kIdxTop, "allocator")) {                                             // L_: settings allocator|nil|"protected"
    case LuaType::NIL:
        // nothing else to do
        break;

    case LuaType::STRING:
        LUA_ASSERT(L_, luaW_tostring(L_, kIdxTop) == "protected");
        // set the original allocator to call from inside protection by the mutex
        protectedAllocator.installIn(L_);
        // before a state is created, this function will be called to obtain the allocator
        provideAllocator = luaW_provide_protected_allocator;
        break;

    case LuaType::FUNCTION:
        provideAllocator = lua_tocfunction(L_, -1);                                                // L_: settings allocator
        if (provideAllocator != nullptr) {
            // make sure the function doesn't have upvalues
            char const* _upname{ lua_getupvalue(L_, -1, 1) };                                      // L_: settings allocator upval?
            if (_upname != nullptr) {   // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "config.allocator() shouldn't have upvalues");
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings allocator nil
            lua_setfield(L_, -3, "allocator");                                                     // L_: settings allocator
        } else {
            raise_luaL_error(L_, "Bad config.allocator, must be a C function");
        }
        break;

    default: // should be filtered out in lanes.lua
        raise_luaL_error(L_, "Bad config.allocator type %s", luaW_typename(L_, kIdxTop).data());
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);

    std::ignore = luaW_getfield(L_, kIdxTop, "internal_allocator");                                // L_: settings "libc"|"allocator"
    LUA_ASSERT(L_, lua_isstring(L_, kIdxTop)); // should be the case due to lanes.lua parameter validation
    std::string_view const _allocator{ luaW_tostring(L_, kIdxTop) };
    // use whatever the provider provides. This performs validation of what provideAllocator is giving
    // we do this even if _allocator == "libc", to have the validation part
    internalAllocator = resolveAndValidateAllocator(L_, "internal");
    if (_allocator == "libc") {
        internalAllocator = lanes::AllocatorDefinition{ libc_lua_Alloc, nullptr };
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
    if (luaW_type(L_, kIdxTop) != LuaType::USERDATA) {
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

void Universe::initializeOnStateCreate(lua_State* const L_)
{
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: settings
    if (luaW_getfield(L_, kIdxTop, kOnStateCreate) != LuaType::NIL) {                              // L_: settings on_state_create|nil
        LUA_ASSERT(L_, luaW_type(L_, kIdxTop) == LuaType::FUNCTION); // ensured by lanes.lua parameter validation
        // store C function pointer in an internal variable
        lua_CFunction const _func{ lua_tocfunction(L_, -1) };                                      // L_: settings on_state_create
        if (_func) {
            // make sure the function doesn't have upvalues
            char const* _upname{ lua_getupvalue(L_, -1, 1) };                                      // L_: settings on_state_create upval?
            if (_upname != nullptr) { // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "%s shouldn't have upvalues", kOnStateCreate.data());
            }
            onStateCreateFunc.emplace<lua_CFunction>(_func);
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings on_state_create nil
            luaW_setfield(L_, StackIndex{ -3 }, kOnStateCreate);                                   // L_: settings on_state_create
        } else {
            // the function is still in the config table. we indicate this with the uintptr_t alternative (actual value is irrelevant)
            onStateCreateFunc.emplace<uintptr_t>(std::bit_cast<uintptr_t>(kOnStateCreate.data()));
        }
    } else {
        LUA_ASSERT(L_, std::holds_alternative<std::nullptr_t>(onStateCreateFunc));
    };
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 0);
}

// #################################################################################################

lanes::AllocatorDefinition Universe::resolveAndValidateAllocator(lua_State* const L_, std::string_view const& hint_) const
{
    lanes::AllocatorDefinition _ret{ protectedAllocator };
    if (provideAllocator == nullptr) {
        return _ret;
    }

    STACK_CHECK_START_REL(L_, 0); // here, we have a function we can call to obtain an allocator
    lua_pushcclosure(L_, provideAllocator, 0);                                                     // L_: provideAllocator()
    luaW_pushstring(L_, hint_);                                                                    // L_: provideAllocator() "<hint>"
    lua_call(L_, 1, 1);                                                                            // L_: result
    // make sure we have a valid AllocatorDefinition on the stack (an error is raised instead if it is not the case)
    _ret = lanes::AllocatorDefinition::Validated(L_, kIdxTop);
    lua_pop(L_, 1);                                                                                // L_:
    STACK_CHECK(L_, 0);
    return _ret;
}

// #################################################################################################

bool Universe::terminateFreeRunningLanes(lua_Duration const shutdownTimeout_, CancelOp const op_)
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
                    std::ignore = _lane->cancel(op_, std::chrono::steady_clock::now() + 1us, WakeLane::Yes, 1);
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
                        if (_lane->cancelRequest.load(std::memory_order_relaxed) != CancelRequest::None)
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

    // are all lanes successfully terminated?
    return selfdestructFirst == SELFDESTRUCT_END;
}

// #################################################################################################

// process end: cancel any still free-running threads
// as far as I can tell, this can only by called only from lua_close()
int Universe::UniverseGC(lua_State* const L_)
{
    lua_Duration const _shutdown_timeout{ lua_tonumber(L_, lua_upvalueindex(1)) };
    STACK_CHECK_START_ABS(L_, 1);
    Universe* const _U{ luaW_tofulluserdata<Universe>(L_, StackIndex{ 1 }) };                      // L_: U

    // attempt to terminate all lanes with increasingly stronger cancel methods
    bool const _allLanesTerminated{ 
        _U->terminateFreeRunningLanes(_shutdown_timeout, { CancelRequest::Soft, LuaHookMask::None })
        || _U->terminateFreeRunningLanes(_shutdown_timeout, { CancelRequest::Hard, LuaHookMask::None })
        || _U->terminateFreeRunningLanes(_shutdown_timeout, { CancelRequest::Hard, LuaHookMask::All })
    };

    // invoke the function installed by lanes.finally()
    kFinalizerRegKey.pushValue(L_);                                                                // L_: U finalizer|nil
    if (!lua_isnil(L_, -1)) {
        lua_pushboolean(L_, _allLanesTerminated);                                                  // L_: U finalizer bool
        // no protection. Lua rules for errors in finalizers apply normally:
        // Lua 5.4+: error is propagated in the warn system
        // older: error is swallowed
        lua_call(L_, 1, 1);                                                                        // L_: U msg?
        // phew, no error in finalizer, since we reached that point
    }

    if (lua_isnil(L_, kIdxTop)) {
        lua_pop(L_, 1);                                                                            // L_: U
        // no finalizer, or it returned no value: push some default message on the stack, in case it is necessary
        luaW_pushstring(L_, "uncooperative lanes detected at shutdown");                           // L_: U "msg"
    }
    STACK_CHECK(L_, 2);

    {
        std::lock_guard<std::mutex> _guard{ _U->selfdestructMutex };
        // now, all remaining lanes are flagged. if they crash because we remove keepers and the Universe from under them, it is their fault
        bool const _detectedUncooperativeLanes{ _U->selfdestructFirst != SELFDESTRUCT_END };
        if (_detectedUncooperativeLanes) {
            _U->flagDanglingLanes();
            if (luaW_tostring(L_, kIdxTop) == "freeze") {
                std::this_thread::sleep_until(std::chrono::time_point<std::chrono::steady_clock>::max());
            } else {
                // take the value returned by the finalizer (or our default message) and throw it as an error
                // since we are inside Lua's GCTM, it will be propagated through the warning system (Lua 5.4) or swallowed silently
                // IMPORTANT: lua_error() is used here instead of the wrapper raise_lua_error() to circumvent what looks like a MSVC compiler bug
                // that manifests as a crash inside ntdll!longjmp() function, in optimized builds only
                lua_error(L_);
            }
        } else {
            // we didn't use the error message, let's keep a clean stack
            lua_pop(L_, 1); // L_: U
        }
    }
    STACK_CHECK(L_, 1);

    // ---------------------------------------------------------
    // we don't reach that point if some lanes are still running
    // ---------------------------------------------------------

    // no need to mutex-protect this as all lanes in the universe are gone at that point
    Linda::DeleteTimerLinda(L_, std::exchange(_U->timerLinda, nullptr), PK);

    if (!_U->keepers.close()) {
        raise_luaL_error(L_, "INTERNAL ERROR: Keepers closed more than once");
    }

    // remove the protected allocator, if any
    _U->protectedAllocator.removeFrom(L_);

    // no longer found in the registry
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushnil(L_); });
    kUniverseLightRegKey.setValue(L_, [](lua_State* L_) { lua_pushnil(L_); });
    _U->Universe::~Universe();

    return 0;
}
