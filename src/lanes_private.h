#pragma once

#include "uniquekey.h"
#include "cancel.h"
#include "universe.h"

enum class ThreadStatus
{
    Normal, // normal master side state
    Killed  // issued an OS kill
};

// NOTE: values to be changed by either thread, during execution, without
//       locking, are marked "volatile"
//
struct Lane
{
    THREAD_T thread;
    //
    // M: sub-thread OS thread
    // S: not used

    char const* debug_name;

    lua_State* L;
    Universe* U;
    //
    // M: prepares the state, and reads results
    // S: while S is running, M must keep out of modifying the state

    volatile enum e_status status;
    // 
    // M: sets to PENDING (before launching)
    // S: updates -> RUNNING/WAITING -> DONE/ERROR_ST/CANCELLED

    SIGNAL_T* volatile waiting_on;
    //
    // When status is WAITING, points on the linda's signal the thread waits on, else nullptr

    volatile CancelRequest cancel_request;
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

    volatile ThreadStatus mstatus;
    //
    // M: sets to Normal, if issued a kill changes to Killed
    // S: not used

    Lane* volatile selfdestruct_next;
    //
    // M: sets to non-nullptr if facing lane handle '__gc' cycle but the lane
    //    is still running
    // S: cleans up after itself if non-nullptr at lane exit

#if HAVE_LANE_TRACKING()
    Lane* volatile tracking_next;
#endif // HAVE_LANE_TRACKING()
    //
    // For tracking only
};

// To allow free-running threads (longer lifespan than the handle's)
// 'Lane' are malloc/free'd and the handle only carries a pointer.
// This is not deep userdata since the handle's not portable among lanes.
//
#define lua_toLane( L, i) (*((Lane**) luaL_checkudata( L, i, "Lane")))

static inline Lane* get_lane_from_registry( lua_State* L)
{
    Lane* s;
    STACK_GROW( L, 1);
    STACK_CHECK( L, 0);
    CANCEL_TEST_KEY.query_registry(L);
    s = (Lane*) lua_touserdata( L, -1);     // lightuserdata (true 's_lane' pointer) / nil
    lua_pop( L, 1);
    STACK_END( L, 0);
    return s;
}

int push_thread_status( lua_State* L, Lane* s);
