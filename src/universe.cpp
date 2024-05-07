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

#include "cancel.h"
#include "compat.h"
#include "deep.h"
#include "keeper.h"
#include "lanes_private.h"
#include "tools.h"

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
    Universe* const U{ lua_newuserdatauv<Universe>(L_, 0) }; // universe
    U->Universe::Universe();
    STACK_CHECK_START_REL(L_, 1);
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    kUniverseLightRegKey.setValue(L_, [U](lua_State* L_) { lua_pushlightuserdata(L_, U); });
    STACK_CHECK(L_, 1);
    return U;
}

// #################################################################################################

void Universe::terminateFreeRunningLanes(lua_State* L_, lua_Duration shutdownTimeout_, CancelOp op_)
{
    if (selfdestructFirst != SELFDESTRUCT_END) {
        // Signal _all_ still running threads to exit (including the timer thread)
        {
            std::lock_guard<std::mutex> guard{ selfdestructMutex };
            Lane* lane{ selfdestructFirst };
            while (lane != SELFDESTRUCT_END) {
                // attempt the requested cancel with a small timeout.
                // if waiting on a linda, they will raise a cancel_error.
                // if a cancellation hook is desired, it will be installed to try to raise an error
                if (lane->thread.joinable()) {
                    std::ignore = thread_cancel(lane, op_, 1, std::chrono::steady_clock::now() + 1us, true);
                }
                lane = lane->selfdestruct_next;
            }
        }

        // When noticing their cancel, the lanes will remove themselves from the selfdestruct chain.
        {
            std::chrono::time_point<std::chrono::steady_clock> t_until{ std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(shutdownTimeout_) };

            while (selfdestructFirst != SELFDESTRUCT_END) {
                // give threads time to act on their cancel
                std::this_thread::yield();
                // count the number of cancelled thread that didn't have the time to act yet
                int n{ 0 };
                {
                    std::lock_guard<std::mutex> guard{ selfdestructMutex };
                    Lane* lane{ selfdestructFirst };
                    while (lane != SELFDESTRUCT_END) {
                        if (lane->cancelRequest != CancelRequest::None)
                            ++n;
                        lane = lane->selfdestruct_next;
                    }
                }
                // if timeout elapsed, or we know all threads have acted, stop waiting
                std::chrono::time_point<std::chrono::steady_clock> t_now = std::chrono::steady_clock::now();
                if (n == 0 || (t_now >= t_until)) {
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
        std::lock_guard<std::mutex> guard{ selfdestructMutex };
        Lane* lane{ selfdestructFirst };
        if (lane != SELFDESTRUCT_END) {
            // this causes a leak because we don't call U's destructor (which could be bad if the still running lanes are accessing it)
            raise_luaL_error(L_, "Zombie thread %s refuses to die!", lane->debugName);
        }
    }
}

// #################################################################################################

// process end: cancel any still free-running threads
int universe_gc(lua_State* L_)
{
    lua_Duration const shutdown_timeout{ lua_tonumber(L_, lua_upvalueindex(1)) };
    [[maybe_unused]] char const* const op_string{ lua_tostring(L_, lua_upvalueindex(2)) };
    Universe* const U{ lua_tofulluserdata<Universe>(L_, 1) };
    U->terminateFreeRunningLanes(L_, shutdown_timeout, which_cancel_op(op_string));

    // no need to mutex-protect this as all threads in the universe are gone at that point
    if (U->timerLinda != nullptr) { // test in case some early internal error prevented Lanes from creating the deep timer
        [[maybe_unused]] int const prev_ref_count{ U->timerLinda->refcount.fetch_sub(1, std::memory_order_relaxed) };
        LUA_ASSERT(L_, prev_ref_count == 1); // this should be the last reference
        DeepFactory::DeleteDeepObject(L_, U->timerLinda);
        U->timerLinda = nullptr;
    }

    close_keepers(U);

    // remove the protected allocator, if any
    U->protectedAllocator.removeFrom(L_);

    U->Universe::~Universe();

    return 0;
}
