#pragma once

#include "uniquekey.h"
#include "cancel.h"
#include "universe.h"

// NOTE: values to be changed by either thread, during execution, without
//       locking, are marked "volatile"
//
class Lane
{
    private:

    enum class ThreadStatus
    {
        Normal, // normal master side state
        Killed // issued an OS kill
    };

    public:

    using enum ThreadStatus;

    THREAD_T thread;
    //
    // M: sub-thread OS thread
    // S: not used

    char const* debug_name{ "<unnamed>" };

    Universe* const U;
    lua_State* L;
    //
    // M: prepares the state, and reads results
    // S: while S is running, M must keep out of modifying the state

    volatile enum e_status status{ PENDING };
    // 
    // M: sets to PENDING (before launching)
    // S: updates -> RUNNING/WAITING -> DONE/ERROR_ST/CANCELLED

    SIGNAL_T* volatile waiting_on{ nullptr };
    //
    // When status is WAITING, points on the linda's signal the thread waits on, else nullptr

    volatile CancelRequest cancel_request{ CancelRequest::None };
    //
    // M: sets to false, flags true for cancel request
    // S: reads to see if cancel is requested

#if THREADWAIT_METHOD == THREADWAIT_CONDVAR
    SIGNAL_T done_signal;
    //
    // M: Waited upon at lane ending  (if Posix with no PTHREAD_TIMEDJOIN)
    // S: sets the signal once cancellation is noticed (avoids a kill)

    MUTEX_T done_lock;
    // 
    // Lock required by 'done_signal' condition variable, protecting
    // lane status changes to DONE/ERROR_ST/CANCELLED.
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR

    volatile ThreadStatus mstatus{ Normal };
    //
    // M: sets to Normal, if issued a kill changes to Killed
    // S: not used

    Lane* volatile selfdestruct_next{ nullptr };
    //
    // M: sets to non-nullptr if facing lane handle '__gc' cycle but the lane
    //    is still running
    // S: cleans up after itself if non-nullptr at lane exit

#if HAVE_LANE_TRACKING()
    Lane* volatile tracking_next{ nullptr };
#endif // HAVE_LANE_TRACKING()
    //
    // For tracking only

    static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internal_allocator.alloc(size_); }
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internal_allocator.free(p_, sizeof(Lane)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Lane*>(p_)->U->internal_allocator.free(p_, sizeof(Lane)); }

    Lane(Universe* U_, lua_State* L_);
    ~Lane();
};

// xxh64 of string "LANE_POINTER_REGKEY" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey LANE_POINTER_REGKEY{ 0xB3022205633743BCull }; // used as registry key

// To allow free-running threads (longer lifespan than the handle's)
// 'Lane' are malloc/free'd and the handle only carries a pointer.
// This is not deep userdata since the handle's not portable among lanes.
//
#define lua_toLane( L, i) (*((Lane**) luaL_checkudata( L, i, "Lane")))

int push_thread_status( lua_State* L, Lane* s);
