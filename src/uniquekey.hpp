#pragma once

#include "compat.hpp"
#include "macros_and_utils.hpp"

// #################################################################################################

class UniqueKey
{
    public:
    uintptr_t const storage{ 0 };
    std::string_view debugName{};

    // ---------------------------------------------------------------------------------------------
    constexpr explicit UniqueKey(uint64_t val_, std::string_view const& debugName_ = {})
#if LUAJIT_FLAVOR() == 64 // building against LuaJIT headers for 64 bits, light userdata is restricted to 47 significant bits, because LuaJIT uses the other bits for internal optimizations
    : storage{ static_cast<uintptr_t>(val_ & 0x7FFFFFFFFFFFull) }
#else // LUAJIT_FLAVOR()
    : storage{ static_cast<uintptr_t>(val_) }
#endif // LUAJIT_FLAVOR()
    , debugName{ debugName_ }
    {
    }
    // ---------------------------------------------------------------------------------------------
    // rule of 5
    UniqueKey() = delete;
    constexpr UniqueKey(UniqueKey const&) = default;
    UniqueKey(UniqueKey&&) = delete;
    UniqueKey& operator=(UniqueKey const&) = delete;
    UniqueKey& operator=(UniqueKey&&) = delete;
    // debugName is irrelevant in comparisons
    inline constexpr std::weak_ordering operator<=>(UniqueKey const& rhs_) const { return storage <=> rhs_.storage; }
    inline constexpr auto operator==(UniqueKey const& rhs_) const { return storage == rhs_.storage; }
    // ---------------------------------------------------------------------------------------------
    bool equals(lua_State* const L_, StackIndex const i_) const
    {
        return lua_touserdata(L_, i_) == std::bit_cast<void*>(storage);
    }
    // ---------------------------------------------------------------------------------------------
    void pushKey(lua_State* const L_) const
    {
        lua_pushlightuserdata(L_, std::bit_cast<void*>(storage));
    }
};

// #################################################################################################

DECLARE_UNIQUE_TYPE(NArr, int);
DECLARE_UNIQUE_TYPE(NRec, int);

class RegistryUniqueKey final
: public UniqueKey
{
    public:
    using UniqueKey::UniqueKey;

    // ---------------------------------------------------------------------------------------------
    void pushValue(lua_State* const L_) const
    {
        STACK_CHECK_START_REL(L_, 0);
        pushKey(L_);                                                                               // L_: ... key
        lua_rawget(L_, kIdxRegistry);                                                              // L_: ... value
        STACK_CHECK(L_, 1);
    }
    // ---------------------------------------------------------------------------------------------
    template <typename OP>
    // TODO: add a requirement clause here for operation_ to be callable on L_
    void setValue(lua_State* const L_, OP operation_) const
    {
        // Note we can't check stack consistency because operation is not always a push (could be insert, replace, whatever)
        pushKey(L_);                                                                               // L_: ... key
        operation_(L_);                                                                            // L_: ... key value
        lua_rawset(L_, kIdxRegistry);                                                              // L_: ...
    }
    // ---------------------------------------------------------------------------------------------
    template <typename T>
    [[nodiscard]]
    T* readLightUserDataValue(lua_State* const L_) const
    {
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);
        pushValue(L_);                                                                             // L_: ... {}|nil
        T* const value{ luaW_tolightuserdata<T>(L_, kIdxTop) };
        lua_pop(L_, 1);                                                                            // L_: ...
        STACK_CHECK(L_, 0);
        return value;
    }
    // ---------------------------------------------------------------------------------------------
    [[nodiscard]]
    bool readBoolValue(lua_State* const L_) const
    {
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);
        pushValue(L_);                                                                             // L_: ... bool|nil
        bool const value{ lua_toboolean(L_, -1) ? true : false };
        lua_pop(L_, 1);                                                                            // L_: ...
        STACK_CHECK(L_, 0);
        return value;
    }
    // ---------------------------------------------------------------------------------------------
    // equivalent to luaL_getsubtable
    [[nodiscard]]
    bool getSubTable(lua_State* const L_, NArr const narr_, NRec const nrec_) const
    {
        STACK_CHECK_START_REL(L_, 0);
        pushValue(L_);                                                                             // L_: {}|nil
        if (!lua_isnil(L_, -1)) {
            LUA_ASSERT(L_, lua_istable(L_, -1));
            STACK_CHECK(L_, 1);
            return true; // table already exists
        }
        lua_pop(L_, 1);                                                                            // L_:
        // store a newly created table in the registry, but leave it on the stack too
        lua_createtable(L_, narr_, nrec_);                                                         // L_: {}
        setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });                                // L_: {}
        STACK_CHECK(L_, 1);
        return false;
    }
    // ---------------------------------------------------------------------------------------------
    void getSubTableMode(lua_State* const L_, std::string_view const& mode_) const
    {
        STACK_CHECK_START_REL(L_, 0);
        if (!getSubTable(L_, NArr{ 0 }, NRec{ 0 })) {                                              // L_: {}
            // Set its metatable if requested
            if (!mode_.empty()) {
                STACK_GROW(L_, 3);
                lua_createtable(L_, 0, 1);                                                         // L_: {} mt
                luaW_pushstring(L_, "__mode");                                                     // L_: {} mt "__mode"
                luaW_pushstring(L_, mode_);                                                        // L_: {} mt "__mode" mode
                lua_rawset(L_, -3);                                                                // L_: {} mt
                lua_setmetatable(L_, -2);                                                          // L_: {}
            }
        }
        STACK_CHECK(L_, 1);
    }
};

// #################################################################################################
