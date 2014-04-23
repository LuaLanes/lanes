#if !defined( __COMPAT_H__)
#define __COMPAT_H__ 1

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

// code is now using Lua 5.2 API
// add Lua 5.2 API when building for Lua 5.1
#if LUA_VERSION_NUM == 501
#define lua_absindex( L, idx) (((idx) >= 0 || (idx) <= LUA_REGISTRYINDEX) ? (idx) : lua_gettop(L) + (idx) +1)
#define lua_pushglobaltable(L) lua_pushvalue( L, LUA_GLOBALSINDEX)
#define lua_setuservalue lua_setfenv
#define lua_getuservalue lua_getfenv
#define lua_rawlen lua_objlen
#define luaG_registerlibfuncs( L, _funcs) luaL_register( L, NULL, _funcs)
#define LUA_OK 0
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.1, we don't care about the actual value
void luaL_requiref (lua_State* L, const char* modname, lua_CFunction openf, int glb); // implementation copied from Lua 5.2 sources
#endif // LUA_VERSION_NUM == 501

// wrap Lua 5.2 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 502
#ifndef lua_equal // already defined when compatibility is active in luaconf.h
#define lua_equal( L, a, b) lua_compare( L, a, b, LUA_OPEQ)
#endif // lua_equal
#ifndef lua_lessthan // already defined when compatibility is active in luaconf.h
#define lua_lessthan( L, a, b) lua_compare( L, a, b, LUA_OPLT)
#endif // lua_lessthan
#define luaG_registerlibfuncs( L, _funcs) luaL_setfuncs( L, _funcs, 0)
#endif // LUA_VERSION_NUM == 502

#endif // __COMPAT_H__
