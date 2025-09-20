#pragma once

#include "allocator.hpp"
#include "cancel.hpp"
#include "keeper.hpp"
#include "lanesconf.h"
#include "threading.hpp"
#include "tracker.hpp"
#include "uniquekey.hpp"

// #################################################################################################

// forwards
class Lane;
class Linda;
enum class LookupMode;

// #################################################################################################

// having this enum declared here is not ideal, but it will do for now
enum class [[nodiscard]] ConvertMode
{
    DoNothing, // no conversion
    ConvertToNil, // value is converted to nil
    Decay, // value is converted to a light userdata pointer
    UserConversion // value is converted by calling user-provided function
};

// #################################################################################################

// mutex-protected allocator for use with Lua states that share a non-threadsafe allocator
class ProtectedAllocator final
: public lanes::AllocatorDefinition
{
    private:

    using super = lanes::AllocatorDefinition;

    std::mutex mutex;

    [[nodiscard]]
    static void* Protected_lua_Alloc(void* const ud_, void* const ptr_, size_t const osize_, size_t const nsize_);

    public:
    // we are not like our base class: we can't be created inside a full userdata (or we would have to install a metatable and __gc handler to destroy ourselves properly)
    [[nodiscard]]
    static void* operator new(size_t const size_, lua_State* const L_) noexcept = delete;
    static void operator delete(void* const p_, lua_State* const L_) = delete;

    AllocatorDefinition makeDefinition()
    {
        return AllocatorDefinition{ Protected_lua_Alloc, this };
    }

    void installIn(lua_State* const L_) const
    {
        // install our replacement allocator function (this is a C function, we need to deconst ourselves)
        lua_setallocf(L_, Protected_lua_Alloc, static_cast<void*>(const_cast<ProtectedAllocator*>(this)));
    }

    void removeFrom(lua_State* const L_) const
    {
        // restore the base allocator function
        super::installIn(L_);
    }
};

// #################################################################################################
// #################################################################################################

// xxh64 of string "kUniverseLightRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseLightRegKey{ 0x48BBE9CEAB0BA04Full };

// #################################################################################################

// everything regarding the Lanes universe is stored in that global structure
// held as a full userdata in the master Lua state that required it for the first time
class Universe final
{
    public:
    static constexpr char const* kFinally{ "finally" }; // update lanes.lua if the name changes!

#ifdef PLATFORM_LINUX
    // Linux needs to check, whether it's been run as root
    SudoFlag const sudo{ geteuid() == 0 };
#else
    SudoFlag const sudo{ false };
#endif // PLATFORM_LINUX

    // for verbose errors
    bool verboseErrors{ false };

    bool stripFunctions{ true };

    // before a state is created, this function will be called to obtain the allocator
    lua_CFunction provideAllocator{ nullptr };

    // after a state is created, this function will be called right after the bases libraries are loaded
    std::variant<std::nullptr_t, uintptr_t, lua_CFunction> onStateCreateFunc;

    // if allocator="protected" is found in the configuration settings, a wrapper allocator will protect all allocator calls with a mutex
    // contains a mutex and the original allocator definition
    ProtectedAllocator protectedAllocator;

    lanes::AllocatorDefinition internalAllocator;

    Keepers keepers;

    ConvertMode convertMode{ ConvertMode::DoNothing };
    uint32_t convertMaxAttempts{ 1 };

    lua_Duration lindaWakePeriod{};

    // Initialized by 'init_once_LOCKED()': the deep userdata Linda object
    // used for timers (each lane will get a proxy to this)
    Linda* timerLinda{ nullptr };

    LaneTracker tracker;

    // Protects modifying the selfdestruct chain
    mutable std::mutex selfdestructMutex;

    // require() serialization
    std::recursive_mutex requireMutex;

    // metatable unique identifiers
    std::atomic<lua_Integer> nextMetatableId{ 1 };

#if USE_DEBUG_SPEW()
    std::atomic<int> debugspewIndentDepth{ 0 };
#endif // USE_DEBUG_SPEW()

    // access is protected by selfDestructMutex
    Lane* selfdestructFirst{ nullptr };
    // After a lane has removed itself from the chain, it still performs some processing.
    // The terminal desinit sequence should wait for all such processing to terminate before force-killing threads
    std::atomic<int> selfdestructingCount{ 0 };

    private:
    static int UniverseGC(lua_State* L_);
    void flagDanglingLanes() const;

    public:
    [[nodiscard]]
    static void* operator new([[maybe_unused]] size_t const size_, lua_State* const L_) noexcept { return luaW_newuserdatauv<Universe>(L_, UserValueCount{ 0 }); };
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* const p_, [[maybe_unused]] lua_State* const L_) {} // nothing to do, as nothing is allocated independently

    Universe();
    ~Universe() = default;
    // non-copyable, non-movable
    Universe(Universe const&) = delete;
    Universe(Universe&&) = delete;
    Universe& operator=(Universe const&) = delete;
    Universe& operator=(Universe&&) = delete;

    void callOnStateCreate(lua_State* L_, lua_State* from_, LookupMode mode_);
    [[nodiscard]]
    static Universe* Create(lua_State* L_);
    [[nodiscard]]
    static inline Universe* Get(lua_State* L_);
    void initializeAllocatorFunction(lua_State* L_);
    static int InitializeFinalizer(lua_State* L_);
    void initializeOnStateCreate(lua_State* L_);
    [[nodiscard]]
    lanes::AllocatorDefinition resolveAndValidateAllocator(lua_State* L_, std::string_view const& hint_) const;
    static inline void Store(lua_State* L_, Universe* U_);
    [[nodiscard]]
    bool terminateFreeRunningLanes(lua_Duration shutdownTimeout_, CancelOp op_);
};

// #################################################################################################

[[nodiscard]]
inline Universe* Universe::Get(lua_State* const L_)
{
    STACK_CHECK_START_REL(L_, 0);
    Universe* const _universe{ kUniverseLightRegKey.readLightUserDataValue<Universe>(L_) };
    STACK_CHECK(L_, 0);
    return _universe;
}

// #################################################################################################

inline void Universe::Store(lua_State* const L_, Universe* const U_)
{
    // TODO: check if we actually ever call Store with a null universe
    LUA_ASSERT(L_, !U_ || Universe::Get(L_) == nullptr);
    STACK_CHECK_START_REL(L_, 0);
    kUniverseLightRegKey.setValue(L_, [U = U_](lua_State* L_) { U ? lua_pushlightuserdata(L_, U) : lua_pushnil(L_); });
    STACK_CHECK(L_, 0);
}
