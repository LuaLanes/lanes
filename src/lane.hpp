#pragma once

#include "cancel.hpp"
#include "uniquekey.hpp"
#include "universe.hpp"

// #################################################################################################

// xxh64 of string "kExtendedStackTraceRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kExtendedStackTraceRegKey{ 0x38147AD48FB426E2ull }; // used as registry key

/*
 * registry[FINALIZER_REG_KEY] is either nil (no finalizers) or a table
 * of functions that Lanes will call after the executing 'pcall' has ended.
 *
 * We're NOT using the GC system for finalizer mainly because providing the
 * error (and maybe stack trace) arguments to the finalizer functions would
 * anyways complicate that approach.
 */
// xxh64 of string "kCoroutineRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kCoroutineRegKey{ 0x72B049B0D130F009ull };

// xxh64 of string "kFinalizerRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kFinalizerRegKey{ 0xFE936BFAA718FEEAull };

// xxh64 of string "kLaneGC" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kLaneGC{ 0x5D6122141727F960ull };

// xxh64 of string "kLanePointerRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLanePointerRegKey{ 0x2D8CF03FE9F0A51Aull }; // used as registry key

    // xxh64 of string "debugName" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLaneNameRegKey{ 0xA194E2645C57F6DDull };

// #################################################################################################

// The chain is ended by '(Lane*)(-1)', not nullptr: 'selfdestructFirst -> ... -> ... -> (-1)'
#define SELFDESTRUCT_END ((Lane*) (-1))

static constexpr std::string_view kLaneMetatableName{ "Lane" };

// must be a #define instead of a constexpr to benefit from compile-time string concatenation
#define kLanesLibName "lanes"
#define kLanesCoreLibName kLanesLibName "_core"

// for cancel() argument
enum class [[nodiscard]] WakeLane
{
    No,
    Yes
};

class Lane final
{
    public:
    /*
      Pending: The Lua VM hasn't done anything yet.
      Resuming: The user requested the lane to resume execution from Suspended state.
      Closing: The user is joining the lane, specifically interrupting a suspended Lane.
      Suspended: returned from lua_resume, waiting for the client to request a lua_resume.
      Running, Suspended, Waiting: Thread is inside the Lua VM.
      Done, Error, Cancelled: Thread execution is outside the Lua VM. It can be lua_close()d.
    */
    enum class [[nodiscard]] Status
    {
        Pending,
        Running,
        Suspended,
        Resuming,
        Closing,
        Waiting,
        Done,
        Error,
        Cancelled
    };
    using enum Status;

    enum class [[nodiscard]] ErrorTraceLevel
    {
        Minimal, // no error handler function when running the lane body
        Basic, // lane body errors caught by a error handler
        Extended // same as above, but with more data extracted from the debug infos
    };
    using enum ErrorTraceLevel;

    // the thread
    std::thread thread; // use jthread if we ever need a stop_source
#ifndef __PROSPERO__
    // a latch to wait for the lua_State to be ready
    std::latch ready{ 1 };
#else // __PROSPERO__
    std::atomic_flag ready{};
#endif // __PROSPERO__
    // to wait for stop requests through thread's stop_source
    std::mutex doneMutex;
    std::condition_variable doneCondVar; // use condition_variable_any if waiting for a stop_token
    //
    // M: sub-thread OS thread
    // S: not used

    private:

    mutable std::mutex debugNameMutex;
    std::string_view debugName{ "<unnamed>" };

    public:

    Universe* const U{};
    lua_State* S{}; // the master state of the lane
    lua_State* L{}; // the state we run things in (either S or a lua_newthread() state if we run in coroutine mode)
    int nresults{}; // number of results to extract from the stack (can differ from the actual number of values when in coroutine mode)
    //
    // M: prepares the state, and reads results
    // S: while S is running, M must keep out of modifying the state

    std::atomic<Status> status{ Pending };
    static_assert(std::atomic<Status>::is_always_lock_free);
    //
    // M: sets to Pending (before launching)
    // S: updates -> Running/Waiting/Suspended -> Done/Error/Cancelled

    // accessed under control of status acquire/release semantics
    // When status is Waiting, points on the linda's signal the thread waits on, else nullptr
    std::condition_variable* waiting_on{ nullptr };

    std::atomic<CancelRequest> cancelRequest{ CancelRequest::None };
    static_assert(std::atomic<CancelRequest>::is_always_lock_free);
    //
    // M: sets to false, flags true for cancel request
    // S: reads to see if cancel is requested

    // access is protected by Universe::selfDestructMutex
    Lane* selfdestruct_next{ nullptr };
    //
    // M: sets to non-nullptr if facing lane handle '__gc' cycle but the lane
    //    is still running
    // S: cleans up after itself if non-nullptr at lane exit

    // access is protected by LaneTracker::trackingMutex
    Lane* tracking_next{ nullptr };

    ErrorTraceLevel const errorTraceLevel{ Basic };

    // when Universe is collected, and an uncooperative Lane refuses to terminate, this flag becomes true
    // in case of crash, that's the Lane's fault!
    std::atomic_bool flaggedAfterUniverseGC{ false };

    [[nodiscard]]
    static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internalAllocator.alloc(size_); }
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internalAllocator.free(p_, sizeof(Lane)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Lane*>(p_)->U->internalAllocator.free(p_, sizeof(Lane)); }

    ~Lane();
    Lane(Universe* U_, lua_State* L_, ErrorTraceLevel errorTraceLevel_, bool asCoroutine_);

    // rule of 5
    Lane(Lane const&) = delete;
    Lane(Lane&&) = delete;
    Lane& operator=(Lane const&) = delete;
    Lane& operator=(Lane&&) = delete;

    private:

    [[nodiscard]]
    CancelResult internalCancel(CancelRequest rq_, std::chrono::time_point<std::chrono::steady_clock> until_, WakeLane wakeLane_);

    public:

    void applyDebugName() const;
    [[nodiscard]]
    CancelResult cancel(CancelOp op_, std::chrono::time_point<std::chrono::steady_clock> until_, WakeLane wakeLane_, int hookCount_);
    void closeState()
    {
        L = nullptr;
        nresults = 0;
        lua_close(std::exchange(S, nullptr)); // this collects our coroutine thread at the same time
    }
    [[nodiscard]]
    std::string_view errorTraceLevelString() const;
    [[nodiscard]]
    int errorHandlerCount() const noexcept
    {
        // don't push a error handler when in coroutine mode, as the first lua_resume wants only the function and its arguments on the stack
        return ((errorTraceLevel == Lane::Minimal) || isCoroutine()) ? 0 : 1; 
    }
    [[nodiscard]]
    bool isCoroutine() const noexcept { return S != L; }
    [[nodiscard]]
    std::string_view getDebugName() const
    {
        std::lock_guard<std::mutex> _guard{ debugNameMutex };
        return debugName;
    }
    static int LuaErrorHandler(lua_State* L_);
    [[nodiscard]]
    int pushErrorHandler() const noexcept { return (errorHandlerCount() == 0) ? 0 : (lua_pushcfunction(L, LuaErrorHandler), 1); }
    [[nodiscard]]
    std::string_view pushErrorTraceLevel(lua_State* L_) const;
    static void PushMetatable(lua_State* L_);
    void pushStatusString(lua_State* L_) const;
    void pushIndexedResult(lua_State* L_, int key_) const;
    [[nodiscard]]
    int pushStoredResults(lua_State* L_) const;
    void resetResultsStorage(lua_State* L_, StackIndex self_idx_);
    void selfdestructAdd();
    [[nodiscard]]
    bool selfdestructRemove();
    void securizeDebugName(lua_State* L_);
    void startThread(lua_State* L_, int priority_, NativePrioFlag native_);
    void storeDebugName( std::string_view const& name_);
    [[nodiscard]]
    int storeResults(lua_State* L_);
    [[nodiscard]]
    std::string_view threadStatusString() const;
    // wait until the lane stops working with its state (either Suspended or Done+)
    [[nodiscard]]
    bool waitForCompletion(std::chrono::time_point<std::chrono::steady_clock> until_, bool const _acceptSuspended);
    [[nodiscard]]
    bool waitForJoin(lua_State* _L, std::chrono::time_point<std::chrono::steady_clock> until_);
};

// #################################################################################################

// To allow free-running threads (longer lifespan than the handle's)
// 'Lane' are malloc/free'd and the handle only carries a pointer.
// This is not deep userdata since the handle is not portable among lanes.
//
[[nodiscard]]
static inline Lane* ToLane(lua_State* const L_, StackIndex const i_)
{
    return *(static_cast<Lane**>(luaL_checkudata(L_, i_, kLaneMetatableName.data())));
}
