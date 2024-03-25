#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "macros_and_utils.h"

#include <mutex>

// forwards
struct DeepPrelude;
struct Keepers;
struct Lane;

// ################################################################################################

/*
* Do we want to activate full lane tracking feature? (EXPERIMENTAL)
*/
#define HAVE_LANE_TRACKING() 1

// ################################################################################################

// everything we need to provide to lua_newstate()
struct AllocatorDefinition
{
    lua_Alloc allocF{ nullptr };
    void* allocUD{ nullptr };
};

// mutex-protected allocator for use with Lua states that share a non-threadsafe allocator
struct ProtectedAllocator
{
    AllocatorDefinition definition;
    std::mutex lock;
};

// ################################################################################################

// everything regarding the Lanes universe is stored in that global structure
// held as a full userdata in the master Lua state that required it for the first time
// don't forget to initialize all members in LG_configure()
struct Universe
{
    // for verbose errors
    bool verboseErrors{ false };

    bool demoteFullUserdata{ false };

    // before a state is created, this function will be called to obtain the allocator
    lua_CFunction provide_allocator{ nullptr };

    // after a state is created, this function will be called right after the bases libraries are loaded
    lua_CFunction on_state_create_func{ nullptr };

    // if allocator="protected" is found in the configuration settings, a wrapper allocator will protect all allocator calls with a mutex
    // contains a mutex and the original allocator definition
    ProtectedAllocator protected_allocator;

    AllocatorDefinition internal_allocator;

    Keepers* keepers{ nullptr };

    // Initialized by 'init_once_LOCKED()': the deep userdata Linda object
    // used for timers (each lane will get a proxy to this)
    volatile DeepPrelude* timer_deep{ nullptr }; // = nullptr

#if HAVE_LANE_TRACKING()
    std::mutex tracking_cs;
    Lane* volatile tracking_first{ nullptr }; // will change to TRACKING_END if we want to activate tracking
#endif // HAVE_LANE_TRACKING()

    std::mutex selfdestruct_cs;

    // require() serialization
    std::recursive_mutex require_cs;

    std::atomic<lua_Integer> last_mt_id{ 0 };

#if USE_DEBUG_SPEW()
    int debugspew_indent_depth{ 0 };
#endif // USE_DEBUG_SPEW()

    Lane* volatile selfdestruct_first{ nullptr };
    // After a lane has removed itself from the chain, it still performs some processing.
    // The terminal desinit sequence should wait for all such processing to terminate before force-killing threads
    int volatile selfdestructing_count{ 0 };
};

Universe* universe_get(lua_State* L);
Universe* universe_create(lua_State* L);
void universe_store(lua_State* L, Universe* U);
