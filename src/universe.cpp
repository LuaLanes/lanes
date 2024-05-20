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

#include "universe.h"

#include "deep.h"
#include "intercopycontext.h"
#include "keeper.h"
#include "lane.h"
#include "state.h"

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
[[nodiscard]] Universe* universe_create(lua_State* L_)
{
    LUA_ASSERT(L_, universe_get(L_) == nullptr);
    Universe* const _U{ lua_newuserdatauv<Universe>(L_, 0) }; // universe
    _U->Universe::Universe();
    STACK_CHECK_START_REL(L_, 1);
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    kUniverseLightRegKey.setValue(L_, [U = _U](lua_State* L_) { lua_pushlightuserdata(L_, U); });
    STACK_CHECK(L_, 1);
    return _U;
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

/*
 * Pool of keeper states
 *
 * Access to keeper states is locked (only one OS thread at a time) so the
 * bigger the pool, the less chances of unnecessary waits. Lindas map to the
 * keepers randomly, by a hash.
 */

// called as __gc for the keepers array userdata
void Universe::closeKeepers()
{
    if (keepers != nullptr) {
        int _nbKeepers{ keepers->nb_keepers };
        // NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
        // when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
        // in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
        // which is early-outed with a keepers->nbKeepers null-check
        keepers->nb_keepers = 0;
        for (int _i = 0; _i < _nbKeepers; ++_i) {
            lua_State* const _K{ keepers->keeper_array[_i].L };
            keepers->keeper_array[_i].L = KeeperState{ nullptr };
            if (_K != nullptr) {
                lua_close(_K);
            } else {
                // detected partial init: destroy only the mutexes that got initialized properly
                _nbKeepers = _i;
            }
        }
        for (int _i = 0; _i < _nbKeepers; ++_i) {
            keepers->keeper_array[_i].~Keeper();
        }
        // free the keeper bookkeeping structure
        internalAllocator.free(keepers, sizeof(Keepers) + (_nbKeepers - 1) * sizeof(Keeper));
        keepers = nullptr;
    }
}

// #################################################################################################

// called once at the creation of the universe (therefore L_ is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
void Universe::initializeAllocatorFunction(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    lua_getfield(L_, -1, "allocator");                                                             // L_: settings allocator|nil|"protected"
    if (!lua_isnil(L_, -1)) {
        // store C function pointer in an internal variable
        provideAllocator = lua_tocfunction(L_, -1);                                            // L_: settings allocator
        if (provideAllocator != nullptr) {
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

    lua_getfield(L_, -1, "internal_allocator");                                                    // L_: settings "libc"|"allocator"
    {
        char const* const _allocator{ lua_tostring(L_, -1) };
        if (strcmp(_allocator, "libc") == 0) {
            internalAllocator = AllocatorDefinition{ libc_lua_Alloc, nullptr };
        } else if (provideAllocator == luaG_provide_protected_allocator) {
            // user wants mutex protection on the state's allocator. Use protection for our own allocations too, just in case.
            internalAllocator = protectedAllocator.makeDefinition();
        } else {
            // no protection required, just use whatever we have as-is.
            internalAllocator = protectedAllocator;
        }
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);
}

// #################################################################################################

/*
 * Initialize keeper states
 *
 * If there is a problem, returns nullptr and pushes the error message on the stack
 * else returns the keepers bookkeeping structure.
 *
 * Note: Any problems would be design flaws; the created Lua state is left
 *       unclosed, because it does not really matter. In production code, this
 *       function never fails.
 * settings table is expected at position 1 on the stack
 */
void Universe::initializeKeepers(lua_State* L_)
{
    LUA_ASSERT(L_, lua_gettop(L_) == 1 && lua_istable(L_, 1));
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: settings
    lua_getfield(L_, 1, "nb_keepers");                                                             // L_: settings nb_keepers
    int const _nb_keepers{ static_cast<int>(lua_tointeger(L_, -1)) };
    lua_pop(L_, 1);                                                                                // L_: settings
    if (_nb_keepers < 1) {
        raise_luaL_error(L_, "Bad number of keepers (%d)", _nb_keepers);
    }
    STACK_CHECK(L_, 0);

    lua_getfield(L_, 1, "keepers_gc_threshold");                                                   // L_: settings keepers_gc_threshold
    int const keepers_gc_threshold{ static_cast<int>(lua_tointeger(L_, -1)) };
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 0);

    // Keepers contains an array of 1 Keeper, adjust for the actual number of keeper states
    {
        size_t const bytes = sizeof(Keepers) + (_nb_keepers - 1) * sizeof(Keeper);
        keepers = static_cast<Keepers*>(internalAllocator.alloc(bytes));
        if (keepers == nullptr) {
            raise_luaL_error(L_, "out of memory while creating keepers");
        }
        keepers->Keepers::Keepers();
        keepers->gc_threshold = keepers_gc_threshold;
        keepers->nb_keepers = _nb_keepers;

        for (int _i = 0; _i < _nb_keepers; ++_i) {
            keepers->keeper_array[_i].Keeper::Keeper();
        }
    }
    for (int _i = 0; _i < _nb_keepers; ++_i) {
        // note that we will leak K if we raise an error later
        KeeperState const _K{ create_state(this, L_) };                                            // L_: settings                                    K:
        if (_K == nullptr) {
            raise_luaL_error(L_, "out of memory while creating keeper states");
        }

        keepers->keeper_array[_i].L = _K;

        if (keepers->gc_threshold >= 0) {
            lua_gc(_K, LUA_GCSTOP, 0);
        }

        STACK_CHECK_START_ABS(_K, 0);

        // copy the universe pointer in the keeper itself
        universe_store(_K, this);
        STACK_CHECK(_K, 0);

        // make sure 'package' is initialized in keeper states, so that we have require()
        // this because this is needed when transferring deep userdata object
        luaL_requiref(_K, LUA_LOADLIBNAME, luaopen_package, 1);                                    // L_: settings                                    K: package
        lua_pop(_K, 1);                                                                            // L_: settings                                    K:
        STACK_CHECK(_K, 0);
        serialize_require(DEBUGSPEW_PARAM_COMMA(this) _K);
        STACK_CHECK(_K, 0);

        // copy package.path and package.cpath from the source state
        if (luaG_getmodule(L_, LUA_LOADLIBNAME) != LuaType::NIL) {                                 // L_: settings package                            K:
            // when copying with mode LookupMode::ToKeeper, error message is pushed at the top of the stack, not raised immediately
            InterCopyContext _c{ this, DestState{ _K }, SourceState{ L_ }, {}, SourceIndex{ lua_absindex(L_, -1) }, {}, LookupMode::ToKeeper, {} };
            if (_c.inter_copy_package() != InterCopyResult::Success) {                             // L_: settings ... error_msg                      K:
                // if something went wrong, the error message is at the top of the stack
                lua_remove(L_, -2);                                                                // L_: settings error_msg
                raise_lua_error(L_);
            }
        }
        lua_pop(L_, 1);                                                                            // L_: settings                                    K:
        STACK_CHECK(L_, 0);
        STACK_CHECK(_K, 0);

        // attempt to call on_state_create(), if we have one and it is a C function
        // (only support a C function because we can't transfer executable Lua code in keepers)
        // will raise an error in L_ in case of problem
        CallOnStateCreate(this, _K, L_, LookupMode::ToKeeper);

        // to see VM name in Decoda debugger
        lua_pushfstring(_K, "Keeper #%d", _i + 1);                                                 // L_: settings                                    K: "Keeper #n"
        lua_setglobal(_K, "decoda_name");                                                          // L_: settings                                    K:
        // create the fifos table in the keeper state
        Keepers::CreateFifosTable(_K);
        STACK_CHECK(_K, 0);
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

void Universe::terminateFreeRunningLanes(lua_State* L_, lua_Duration shutdownTimeout_, CancelOp op_)
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
                int n{ 0 };
                {
                    std::lock_guard<std::mutex> _guard{ selfdestructMutex };
                    Lane* _lane{ selfdestructFirst };
                    while (_lane != SELFDESTRUCT_END) {
                        if (_lane->cancelRequest != CancelRequest::None)
                            ++n;
                        _lane = _lane->selfdestruct_next;
                    }
                }
                // if timeout elapsed, or we know all threads have acted, stop waiting
                std::chrono::time_point<std::chrono::steady_clock> _now = std::chrono::steady_clock::now();
                if (n == 0 || (_now >= _until)) {
                    DEBUGSPEW_CODE(fprintf(stderr, "%d uncancelled lane(s) remain after waiting %fs at process end.\n", n, shutdownTimeout_.count()));
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
            raise_luaL_error(L_, "Zombie thread %s refuses to die!", _lane->debugName);
        }
    }
}

// #################################################################################################

// process end: cancel any still free-running threads
int universe_gc(lua_State* L_)
{
    lua_Duration const _shutdown_timeout{ lua_tonumber(L_, lua_upvalueindex(1)) };
    std::string_view const _op_string{ lua_tostringview(L_, lua_upvalueindex(2)) };
    Universe* const _U{ lua_tofulluserdata<Universe>(L_, 1) };
    _U->terminateFreeRunningLanes(L_, _shutdown_timeout, which_cancel_op(_op_string));

    // no need to mutex-protect this as all threads in the universe are gone at that point
    if (_U->timerLinda != nullptr) { // test in case some early internal error prevented Lanes from creating the deep timer
        [[maybe_unused]] int const _prev_ref_count{ _U->timerLinda->refcount.fetch_sub(1, std::memory_order_relaxed) };
        LUA_ASSERT(L_, _prev_ref_count == 1); // this should be the last reference
        DeepFactory::DeleteDeepObject(L_, _U->timerLinda);
        _U->timerLinda = nullptr;
    }

    _U->closeKeepers();

    // remove the protected allocator, if any
    _U->protectedAllocator.removeFrom(L_);

    _U->Universe::~Universe();

    return 0;
}
