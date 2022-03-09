#if !defined __lanes_private_h__
#define __lanes_private_h__ 1

#include "uniquekey.h"
#include "cancel.h"
#include "universe.h"

// NOTE: values to be changed by either thread, during execution, without
//       locking, are marked "volatile"
//
struct s_Lane
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
    // When status is WAITING, points on the linda's signal the thread waits on, else NULL

    volatile enum e_cancel_request cancel_request;
    //
    // M: sets to FALSE, flags TRUE for cancel request
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

    volatile enum
    {
        NORMAL,         // normal master side state
        KILLED          // issued an OS kill
    } mstatus;
    //
    // M: sets to NORMAL, if issued a kill changes to KILLED
    // S: not used

    struct s_Lane* volatile selfdestruct_next;
    //
    // M: sets to non-NULL if facing lane handle '__gc' cycle but the lane
    //    is still running
    // S: cleans up after itself if non-NULL at lane exit

#if HAVE_LANE_TRACKING()
    struct s_Lane* volatile tracking_next;
#endif // HAVE_LANE_TRACKING()
    //
    // For tracking only
};
typedef struct s_Lane Lane;

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
    REGISTRY_GET( L, CANCEL_TEST_KEY);
    s = lua_touserdata( L, -1);     // lightuserdata (true 's_lane' pointer) / nil
    lua_pop( L, 1);
    STACK_END( L, 0);
    return s;
}

int push_thread_status( lua_State* L, Lane* s);


#endif // __lanes_private_h__