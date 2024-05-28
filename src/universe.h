#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "lanesconf.h"
#include "tracker.h"
#include "uniquekey.h"

#include <mutex>

// #################################################################################################

// forwards
enum class CancelOp;
struct DeepPrelude;
struct Keepers;
class Lane;

// #################################################################################################

// everything we need to provide to lua_newstate()
class AllocatorDefinition
{
    public:
    lua_Alloc allocF{ nullptr };
    void* allocUD{ nullptr };

    [[nodiscard]] static void* operator new(size_t size_) noexcept = delete; // can't create one outside of a Lua state
    [[nodiscard]] static void* operator new(size_t size_, lua_State* L_) noexcept { return lua_newuserdatauv(L_, size_, 0); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* p_, [[maybe_unused]] lua_State* L_) { LUA_ASSERT(L_, !"should never be called"); }

    AllocatorDefinition(lua_Alloc allocF_, void* allocUD_) noexcept
    : allocF{ allocF_ }
    , allocUD{ allocUD_ }
    {
    }
    AllocatorDefinition() = default;
    AllocatorDefinition(AllocatorDefinition const& rhs_) = default;
    AllocatorDefinition(AllocatorDefinition&& rhs_) = default;
    AllocatorDefinition& operator=(AllocatorDefinition const& rhs_) = default;
    AllocatorDefinition& operator=(AllocatorDefinition&& rhs_) = default;

    void initFrom(lua_State* L_)
    {
        allocF = lua_getallocf(L_, &allocUD);
    }

    void* alloc(size_t nsize_)
    {
        return allocF(allocUD, nullptr, 0, nsize_);
    }

    void free(void* ptr_, size_t osize_)
    {
        std::ignore = allocF(allocUD, ptr_, osize_, 0);
    }
};

// #################################################################################################

// mutex-protected allocator for use with Lua states that share a non-threadsafe allocator
class ProtectedAllocator
: public AllocatorDefinition
{
    private:
    std::mutex mutex;

    [[nodiscard]] static void* protected_lua_Alloc(void* ud_, void* ptr_, size_t osize_, size_t nsize_)
    {
        ProtectedAllocator* const allocator{ static_cast<ProtectedAllocator*>(ud_) };
        std::lock_guard<std::mutex> guard{ allocator->mutex };
        return allocator->allocF(allocator->allocUD, ptr_, osize_, nsize_);
    }

    public:
    // we are not like our base class: we can't be created inside a full userdata (or we would have to install a metatable and __gc handler to destroy ourselves properly)
    [[nodiscard]] static void* operator new(size_t size_, lua_State* L_) noexcept = delete;
    static void operator delete(void* p_, lua_State* L_) = delete;

    AllocatorDefinition makeDefinition()
    {
        return AllocatorDefinition{ protected_lua_Alloc, this };
    }

    void installIn(lua_State* L_)
    {
        lua_setallocf(L_, protected_lua_Alloc, this);
    }

    void removeFrom(lua_State* L_)
    {
        // remove the protected allocator, if any
        if (allocF != nullptr) {
            // install the non-protected allocator
            lua_setallocf(L_, allocF, allocUD);
        }
    }
};

// #################################################################################################

// xxh64 of string "kUniverseFullRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseFullRegKey{ 0x1C2D76870DD9DD9Full };
// xxh64 of string "kUniverseLightRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseLightRegKey{ 0x48BBE9CEAB0BA04Full };

// #################################################################################################

// everything regarding the Lanes universe is stored in that global structure
// held as a full userdata in the master Lua state that required it for the first time
class Universe
{
    public:
    static constexpr char const* kFinally{ "finally" }; // update lanes.lua if the name changes!

#ifdef PLATFORM_LINUX
    // Linux needs to check, whether it's been run as root
    bool const sudo{ geteuid() == 0 };
#else
    bool const sudo{ false };
#endif // PLATFORM_LINUX

    // for verbose errors
    bool verboseErrors{ false };

    bool demoteFullUserdata{ false };

    // before a state is created, this function will be called to obtain the allocator
    lua_CFunction provideAllocator{ nullptr };

    // after a state is created, this function will be called right after the bases libraries are loaded
    lua_CFunction onStateCreateFunc{ nullptr };

    // if allocator="protected" is found in the configuration settings, a wrapper allocator will protect all allocator calls with a mutex
    // contains a mutex and the original allocator definition
    ProtectedAllocator protectedAllocator;

    AllocatorDefinition internalAllocator;

    Keepers* keepers{ nullptr };

    // Initialized by 'init_once_LOCKED()': the deep userdata Linda object
    // used for timers (each lane will get a proxy to this)
    DeepPrelude* timerLinda{ nullptr };

    LaneTracker tracker;

    // Protects modifying the selfdestruct chain
    std::mutex selfdestructMutex;

    // require() serialization
    std::recursive_mutex requireMutex;

    // metatable unique identifiers
    std::atomic<lua_Integer> nextMetatableId{ 1 };

#if USE_DEBUG_SPEW()
    std::atomic<int> debugspewIndentDepth{ 0 };
#endif // USE_DEBUG_SPEW()

    Lane* volatile selfdestructFirst{ nullptr };
    // After a lane has removed itself from the chain, it still performs some processing.
    // The terminal desinit sequence should wait for all such processing to terminate before force-killing threads
    std::atomic<int> selfdestructingCount{ 0 };

    Universe();
    ~Universe() = default;
    // non-copyable, non-movable
    Universe(Universe const&) = delete;
    Universe(Universe&&) = delete;
    Universe& operator=(Universe const&) = delete;
    Universe& operator=(Universe&&) = delete;

    void closeKeepers();
    void initializeAllocatorFunction(lua_State* L_);
    static int InitializeFinalizer(lua_State* L_);
    void initializeKeepers(lua_State* L_);
    void terminateFreeRunningLanes(lua_State* L_, lua_Duration shutdownTimeout_, CancelOp op_);
};

// #################################################################################################

[[nodiscard]] Universe* universe_get(lua_State* L_);
[[nodiscard]] Universe* universe_create(lua_State* L_);
void universe_store(lua_State* L_, Universe* U_);

// #################################################################################################

[[nodiscard]] inline Universe* universe_get(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 0);
    Universe* const _universe{ kUniverseLightRegKey.readLightUserDataValue<Universe>(L_) };
    STACK_CHECK(L_, 0);
    return _universe;
}

// #################################################################################################

inline void universe_store(lua_State* L_, Universe* U_)
{
    LUA_ASSERT(L_, !U_ || universe_get(L_) == nullptr);
    STACK_CHECK_START_REL(L_, 0);
    kUniverseLightRegKey.setValue(L_, [U = U_](lua_State* L_) { U ? lua_pushlightuserdata(L_, U) : lua_pushnil(L_); });
    STACK_CHECK(L_, 0);
}

// #################################################################################################

int universe_gc(lua_State* L_);
