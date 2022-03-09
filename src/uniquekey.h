#if !defined __LANES_UNIQUEKEY_H__
#define __LANES_UNIQUEKEY_H__ 1

#include "compat.h"

// Lua light userdata can hold a pointer.
struct s_UniqueKey
{
    void* value;
};
typedef struct s_UniqueKey UniqueKey;

#if LUAJIT_FLAVOR() == 64 // building against LuaJIT headers for 64 bits, light userdata is restricted to 47 significant bits, because LuaJIT uses the other bits for internal optimizations
#define MAKE_UNIQUE_KEY( p_) ((void*)((ptrdiff_t)(p_) & 0x7fffffffffffull))
#else // LUAJIT_FLAVOR()
#define MAKE_UNIQUE_KEY( p_) ((void*)(ptrdiff_t)(p_))
#endif // LUAJIT_FLAVOR()

#define DECLARE_UNIQUE_KEY( name_) UniqueKey name_
#define DECLARE_CONST_UNIQUE_KEY( name_, p_) UniqueKey const name_ = { MAKE_UNIQUE_KEY( p_)}

#define push_unique_key( L, key_) lua_pushlightuserdata( L, key_.value)
#define equal_unique_key( L, i, key_) (lua_touserdata( L, i) == key_.value)

#endif // __LANES_UNIQUEKEY_H__
