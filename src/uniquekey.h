#pragma once

#include "compat.h"
#include "macros_and_utils.h"

#include <bit>

class UniqueKey
{
    private:

    uintptr_t m_storage;

    public:

    constexpr UniqueKey(uint64_t val_)
#if LUAJIT_FLAVOR() == 64 // building against LuaJIT headers for 64 bits, light userdata is restricted to 47 significant bits, because LuaJIT uses the other bits for internal optimizations
    : m_storage{ static_cast<uintptr_t>(val_ & 0x7fffffffffffull) }
#else // LUAJIT_FLAVOR()
    : m_storage{ static_cast<uintptr_t>(val_) }
#endif // LUAJIT_FLAVOR()
    {
    }
    constexpr UniqueKey(UniqueKey const& rhs_) = default;
    constexpr bool operator!=(UniqueKey const& rhs_) const
    {
        return m_storage != rhs_.m_storage;
    }
    constexpr bool operator==(UniqueKey const& rhs_) const
    {
        return m_storage == rhs_.m_storage;
    }

    void pushKey(lua_State* const L) const
    {
        lua_pushlightuserdata(L, std::bit_cast<void*>(m_storage));
    }
    bool equals(lua_State* const L, int i) const
    {
        return lua_touserdata(L, i) == std::bit_cast<void*>(m_storage);
    }
    void pushValue(lua_State* const L) const
    {
        pushKey(L);
        lua_rawget(L, LUA_REGISTRYINDEX);
    }
    template <typename OP>
    void setValue(lua_State* L, OP operation_) const
    {
        // Note we can't check stack consistency because operation is not always a push (could be insert, replace, whatever)
        pushKey(L);                           // ... key
        operation_(L);                        // ... key value
        lua_rawset(L, LUA_REGISTRYINDEX);     // ...
    }
    template <typename T>
    T* readLightUserDataValue(lua_State* const L) const
    {
        STACK_GROW(L, 1);
        STACK_CHECK_START_REL(L, 0);
        pushValue(L);
        T* const value{ lua_tolightuserdata<T>(L, -1) }; // lightuserdata/nil
        lua_pop(L, 1);
        STACK_CHECK(L, 0);
        return value;
    }
    bool readBoolValue(lua_State* const L) const
    {
        STACK_GROW(L, 1);
        STACK_CHECK_START_REL(L, 0);
        pushValue(L);
        bool const value{ lua_toboolean(L, -1) ? true : false}; // bool/nil
        lua_pop(L, 1);
        STACK_CHECK(L, 0);
        return value;
    }
};
