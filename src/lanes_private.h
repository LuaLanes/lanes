#pragma once

#include "cancel.h"
#include "uniquekey.h"
#include "universe.h"

#include <chrono>
#include <condition_variable>
#include <latch>
#include <stop_token>
#include <thread>

// The chain is ended by '(Lane*)(-1)', not nullptr: 'selfdestructFirst -> ... -> ... -> (-1)'
#define SELFDESTRUCT_END ((Lane*) (-1))

// must be a #define instead of a constexpr to work with lua_pushliteral (until I templatize it)
#define kLaneMetatableName "Lane"
#define kLanesLibName "lanes"
#define kLanesCoreLibName kLanesLibName ".core"

// NOTE: values to be changed by either thread, during execution, without
//       locking, are marked "volatile"
//
class Lane
{
    public:
    /*
      Pending: The Lua VM hasn't done anything yet.
      Running, Waiting: Thread is inside the Lua VM. If the thread is forcefully stopped, we can't lua_close() the Lua State.
      Done, Error, Cancelled: Thread execution is outside the Lua VM. It can be lua_close()d.
    */
    enum class Status
    {
        Pending,
        Running,
        Waiting,
        Done,
        Error,
        Cancelled
    };
    using enum Status;

    // the thread
    std::jthread thread;
    // a latch to wait for the lua_State to be ready
    std::latch ready{ 1 };
    // to wait for stop requests through thread's stop_source
    std::mutex doneMutex;
    std::condition_variable doneCondVar; // use condition_variable_any if waiting for a stop_token
    //
    // M: sub-thread OS thread
    // S: not used

    char const* debugName{ "<unnamed>" };

    Universe* const U;
    lua_State* L;
    //
    // M: prepares the state, and reads results
    // S: while S is running, M must keep out of modifying the state

    Status volatile status{ Pending };
    //
    // M: sets to Pending (before launching)
    // S: updates -> Running/Waiting -> Done/Error/Cancelled

    std::condition_variable* volatile waiting_on{ nullptr };
    //
    // When status is Waiting, points on the linda's signal the thread waits on, else nullptr

    CancelRequest volatile cancelRequest{ CancelRequest::None };
    //
    // M: sets to false, flags true for cancel request
    // S: reads to see if cancel is requested

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

    [[nodiscard]] static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internalAllocator.alloc(size_); }
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internalAllocator.free(p_, sizeof(Lane)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Lane*>(p_)->U->internalAllocator.free(p_, sizeof(Lane)); }

    Lane(Universe* U_, lua_State* L_);
    ~Lane();

    [[nodiscard]] bool waitForCompletion(std::chrono::time_point<std::chrono::steady_clock> until_);
    void startThread(int priority_);
    void pushThreadStatus(lua_State* L_);
    void changeDebugName(int nameIdx_);
    void securizeDebugName(lua_State* L_);
};

// xxh64 of string "kLanePointerRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLanePointerRegKey{ 0x2D8CF03FE9F0A51Aull }; // used as registry key

// To allow free-running threads (longer lifespan than the handle's)
// 'Lane' are malloc/free'd and the handle only carries a pointer.
// This is not deep userdata since the handle's not portable among lanes.
//
[[nodiscard]] inline Lane* ToLane(lua_State* L_, int i_)
{
    return *(static_cast<Lane**>(luaL_checkudata(L_, i_, kLaneMetatableName)));
}
