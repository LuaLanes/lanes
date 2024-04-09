#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "compat.h"
#include "macros_and_utils.h"

#include <mutex>

// ################################################################################################

// forwards
struct DeepPrelude;
struct Keepers;
class Lane;

/*
* Do we want to activate full lane tracking feature? (EXPERIMENTAL)
*/
#define HAVE_LANE_TRACKING() 1

// ################################################################################################

// everything we need to provide to lua_newstate()
class AllocatorDefinition
{
    public:

    lua_Alloc m_allocF{ nullptr };
    void* m_allocUD{ nullptr };

    static void* operator new(size_t size_, lua_State* L) noexcept { return lua_newuserdatauv(L, size_, 0); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* p_, lua_State* L) { ASSERT_L(!"should never be called") };

    AllocatorDefinition(lua_Alloc allocF_, void* allocUD_) noexcept
    : m_allocF{ allocF_ }
    , m_allocUD{ allocUD_ }
    {
    }
    AllocatorDefinition() = default;
    AllocatorDefinition(AllocatorDefinition const& rhs_) = default;
    AllocatorDefinition(AllocatorDefinition&& rhs_) = default;
    AllocatorDefinition& operator=(AllocatorDefinition const& rhs_) = default;
    AllocatorDefinition& operator=(AllocatorDefinition&& rhs_) = default;

    void initFrom(lua_State* L)
    {
        m_allocF = lua_getallocf(L, &m_allocUD);
    }

    void* lua_alloc(void* ptr_, size_t osize_, size_t nsize_)
    {
        m_allocF(m_allocUD, ptr_, osize_, nsize_);
    }

    void* alloc(size_t nsize_)
    {
        return m_allocF(m_allocUD, nullptr, 0, nsize_);
    }

    void free(void* ptr_, size_t osize_)
    {
        std::ignore = m_allocF(m_allocUD, ptr_, osize_, 0);
    }
};

// ################################################################################################

// mutex-protected allocator for use with Lua states that share a non-threadsafe allocator
class ProtectedAllocator : public AllocatorDefinition
{
    private:

    std::mutex m_lock;

    static void* protected_lua_Alloc(void* ud_, void* ptr_, size_t osize_, size_t nsize_)
    {
        ProtectedAllocator* const allocator{ static_cast<ProtectedAllocator*>(ud_) };
        std::lock_guard<std::mutex> guard{ allocator->m_lock };
        return allocator->m_allocF(allocator->m_allocUD, ptr_, osize_, nsize_);
    }

    public:

    // we are not like our base class: we can't be created inside a full userdata (or we would have to install a metatable and __gc handler to destroy ourselves properly)
    static void* operator new(size_t size_, lua_State* L) noexcept = delete;
    static void operator delete(void* p_, lua_State* L) = delete;

    AllocatorDefinition makeDefinition()
    {
        return AllocatorDefinition{ protected_lua_Alloc, this};
    }

    void installIn(lua_State* L)
    {
        lua_setallocf(L, protected_lua_Alloc, this);
    }

    void removeFrom(lua_State* L)
    {
        // remove the protected allocator, if any
        if (m_allocF != nullptr)
        {
            // install the non-protected allocator
            lua_setallocf(L, m_allocF, m_allocUD);
        }
    }
};

// ################################################################################################

// everything regarding the Lanes universe is stored in that global structure
// held as a full userdata in the master Lua state that required it for the first time
class Universe
{
    public:

#ifdef PLATFORM_LINUX
    // Linux needs to check, whether it's been run as root
    bool const m_sudo{ geteuid() == 0 };
#else
    bool const m_sudo{ false };
#endif // PLATFORM_LINUX

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
    DeepPrelude* timer_deep{ nullptr };

#if HAVE_LANE_TRACKING()
    std::mutex tracking_cs;
    Lane* volatile tracking_first{ nullptr }; // will change to TRACKING_END if we want to activate tracking
#endif // HAVE_LANE_TRACKING()

    std::mutex selfdestruct_cs;

    // require() serialization
    std::recursive_mutex require_cs;

    // metatable unique identifiers
    std::atomic<lua_Integer> next_mt_id{ 1 };

#if USE_DEBUG_SPEW()
    std::atomic<int> debugspew_indent_depth{ 0 };
#endif // USE_DEBUG_SPEW()

    Lane* volatile selfdestruct_first{ nullptr };
    // After a lane has removed itself from the chain, it still performs some processing.
    // The terminal desinit sequence should wait for all such processing to terminate before force-killing threads
    std::atomic<int> selfdestructing_count{ 0 };

    Universe();
    ~Universe() = default;
    Universe(Universe const&) = delete;
    Universe(Universe&&) = delete;
    Universe& operator=(Universe const&) = delete;
    Universe& operator=(Universe&&) = delete;
};

// ################################################################################################

Universe* universe_get(lua_State* L);
Universe* universe_create(lua_State* L);
void universe_store(lua_State* L, Universe* U);
