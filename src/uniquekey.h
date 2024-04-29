#pragma once

#include "compat.h"
#include "macros_and_utils.h"

#include <bit>

// #################################################################################################

class UniqueKey
{
    protected:
    uintptr_t const m_storage{ 0 };

    public:
    char const* m_debugName{ nullptr };

    // ---------------------------------------------------------------------------------------------
    constexpr explicit UniqueKey(uint64_t val_, char const* debugName_ = nullptr)
#if LUAJIT_FLAVOR() == 64 // building against LuaJIT headers for 64 bits, light userdata is restricted to 47 significant bits, because LuaJIT uses the other bits for internal optimizations
    : m_storage{ static_cast<uintptr_t>(val_ & 0x7FFFFFFFFFFFull) }
#else // LUAJIT_FLAVOR()
    : m_storage{ static_cast<uintptr_t>(val_) }
#endif // LUAJIT_FLAVOR()
    , m_debugName{ debugName_ }
    {
    }
    // ---------------------------------------------------------------------------------------------
    constexpr UniqueKey(UniqueKey const& rhs_) = default;
    // ---------------------------------------------------------------------------------------------
    constexpr std::strong_ordering operator<=>(UniqueKey const& rhs_) const = default;
    // ---------------------------------------------------------------------------------------------
    bool equals(lua_State* const L_, int i_) const
    {
        return lua_touserdata(L_, i_) == std::bit_cast<void*>(m_storage);
    }
    // ---------------------------------------------------------------------------------------------
    void pushKey(lua_State* const L_) const
    {
        lua_pushlightuserdata(L_, std::bit_cast<void*>(m_storage));
    }
};

// #################################################################################################

class RegistryUniqueKey
: public UniqueKey
{
    public:
    using UniqueKey::UniqueKey;

    // ---------------------------------------------------------------------------------------------
    void pushValue(lua_State* const L_) const
    {
        STACK_CHECK_START_REL(L_, 0);
        pushKey(L_);
        lua_rawget(L_, LUA_REGISTRYINDEX);
        STACK_CHECK(L_, 1);
    }
    // ---------------------------------------------------------------------------------------------
    template <typename OP>
    void setValue(lua_State* L_, OP operation_) const
    {
        // Note we can't check stack consistency because operation is not always a push (could be insert, replace, whatever)
        pushKey(L_); // ... key
        operation_(L_); // ... key value
        lua_rawset(L_, LUA_REGISTRYINDEX); // ...
    }
    // ---------------------------------------------------------------------------------------------
    template <typename T>
    T* readLightUserDataValue(lua_State* const L_) const
    {
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);
        pushValue(L_);
        T* const value{ lua_tolightuserdata<T>(L_, -1) }; // lightuserdata/nil
        lua_pop(L_, 1);
        STACK_CHECK(L_, 0);
        return value;
    }
    // ---------------------------------------------------------------------------------------------
    bool readBoolValue(lua_State* const L_) const
    {
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);
        pushValue(L_);
        bool const value{ lua_toboolean(L_, -1) ? true : false }; // bool/nil
        lua_pop(L_, 1);
        STACK_CHECK(L_, 0);
        return value;
    }
};

// #################################################################################################
