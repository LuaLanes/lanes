#pragma once

#include "compat.h"
#include "macros_and_utils.h"

class UniqueKey
{
    private:

    uintptr_t m_storage;

    public:

    constexpr UniqueKey(uintptr_t val_)
#if LUAJIT_FLAVOR() == 64 // building against LuaJIT headers for 64 bits, light userdata is restricted to 47 significant bits, because LuaJIT uses the other bits for internal optimizations
    : m_storage{ val_ & 0x7fffffffffffull }
#else // LUAJIT_FLAVOR()
    : m_storage{ val_ }
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

    void push(lua_State* const L) const
    {
        // unfortunately, converting a scalar to a pointer must go through a C cast
        lua_pushlightuserdata(L, (void*) m_storage);
    }
    bool equals(lua_State* const L, int i) const
    {
        // unfortunately, converting a scalar to a pointer must go through a C cast
        return lua_touserdata(L, i) == (void*) m_storage;
    }
    void query_registry(lua_State* const L) const
    {
        push(L);
        lua_rawget(L, LUA_REGISTRYINDEX);
    }
    template <typename OP>
    void set_registry(lua_State* L, OP operation_) const
    {
        // Note we can't check stack consistency because operation is not always a push (could be insert, replace, whatever)
        push(L);                              // ... key
        operation_(L);                        // ... key value
        lua_rawset(L, LUA_REGISTRYINDEX);     // ...
    }
};
