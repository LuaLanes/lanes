#if !defined __LANES_UNIQUEKEY_H__
#define __LANES_UNIQUEKEY_H__ 1

#include "lualib.h"

// Lua light userdata can hold a pointer.
struct s_UniqueKey
{
	void* value;
};
typedef struct s_UniqueKey UniqueKey;

#if defined(LUA_JITLIBNAME) && (defined(__x86_64__) || defined(_M_X64) || defined(__LP64__)) // building against LuaJIT headers, light userdata is restricted to 47 significant bits.
#define MAKE_UNIQUE_KEY( p_) ((void*)((ptrdiff_t)(p_) & 0x7fffffffffffull))
#else // LUA_JITLIBNAME
#define MAKE_UNIQUE_KEY( p_) ((void*)(ptrdiff_t)(p_))
#endif // LUA_JITLIBNAME

#define DECLARE_UNIQUE_KEY( name_) UniqueKey name_
#define DECLARE_CONST_UNIQUE_KEY( name_, p_) UniqueKey const name_ = { MAKE_UNIQUE_KEY( p_)}

#define push_unique_key( L, key_) lua_pushlightuserdata( L, key_.value)
#define equal_unique_key( L, i, key_) (lua_touserdata( L, i) == key_.value)

#endif // __LANES_UNIQUEKEY_H__
