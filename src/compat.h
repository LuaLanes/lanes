#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif // __cplusplus

// try to detect if we are building against LuaJIT or MoonJIT
#if defined(LUA_JITLIBNAME)
#include "luajit.h"
#if (defined(__x86_64__) || defined(_M_X64) || defined(__LP64__))
#define LUAJIT_FLAVOR() 64
#else // 64 bits
#define LUAJIT_FLAVOR() 32
#endif // 64 bits
#else // LUA_JITLIBNAME
#define LUAJIT_FLAVOR() 0
#define LUA_JITLIBNAME "jit"
#endif // LUA_JITLIBNAME

// code is now preferring Lua 5.4 API

// #################################################################################################

// add some Lua 5.3-style API when building for Lua 5.1
#if LUA_VERSION_NUM == 501

#define lua501_equal lua_equal
inline int lua_absindex(lua_State* L_, int idx_)
{
    return (((idx_) >= 0 || (idx_) <= LUA_REGISTRYINDEX) ? (idx_) : lua_gettop(L_) + (idx_) + 1);
}
#if LUAJIT_VERSION_NUM < 20200 // moonjit is 5.1 plus bits of 5.2 that we don't need to wrap
inline void lua_pushglobaltable(lua_State* L_)
{
    lua_pushvalue(L_, LUA_GLOBALSINDEX);
}
#endif // LUAJIT_VERSION_NUM
inline int lua_setuservalue(lua_State* L_, int idx_)
{
    return lua_setfenv(L_, idx_);
}
inline void lua_getuservalue(lua_State* L_, int idx_)
{
    lua_getfenv(L_, idx_);
}
inline size_t lua_rawlen(lua_State* L_, int idx_)
{
    return lua_objlen(L_, idx_);
}
inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const funcs_[])
{
    luaL_register(L_, nullptr, funcs_);
}
// keep as macros to be consistent with Lua headers
#define LUA_OK 0
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.1, we don't care about the actual value
void luaL_requiref(lua_State* L_, const char* modname_, lua_CFunction openf_, int glb_); // implementation copied from Lua 5.2 sources
inline int lua504_dump(lua_State* L_, lua_Writer writer_, void* data_, [[maybe_unused]] int strip_)
{
    return lua_dump(L_, writer_, data_);
}
#define LUA_LOADED_TABLE "_LOADED" // // doesn't exist in Lua 5.1

int luaL_getsubtable(lua_State* L_, int idx_, const char* fname_);

#endif // LUA_VERSION_NUM == 501

// #################################################################################################

// wrap Lua 5.2 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 502

#ifndef lua501_equal // already defined when compatibility is active in luaconf.h
inline int lua501_equal(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPEQ);
}
#endif // lua501_equal
#ifndef lua_lessthan // already defined when compatibility is active in luaconf.h
inline int lua_lessthan(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPLT);
}
#endif // lua_lessthan
inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const funcs_[])
{
    luaL_setfuncs(L_, funcs_, 0);
}
inline int lua504_dump(lua_State* L_, lua_Writer writer_, void* data_, [[maybe_unused]] int strip_)
{
    return lua_dump(L_, writer_, data_);
}
#define LUA_LOADED_TABLE "_LOADED" // // doesn't exist in Lua 5.2

#endif // LUA_VERSION_NUM == 502

// #################################################################################################

#if LUA_VERSION_NUM < 503
// starting with Lua 5.3, lua_getfield returns the type of the value it found
inline int lua503_getfield(lua_State* L_, int idx_, char const* k_)
{
    lua_getfield(L_, idx_, k_);
    return lua_type(L_, -1);
}

#else // LUA_VERSION_NUM >= 503

inline int lua503_getfield(lua_State* L_, int idx_, char const* k_)
{
    return lua_getfield(L_, idx_, k_);
}

#endif // LUA_VERSION_NUM >= 503

// #################################################################################################

// wrap Lua 5.3 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 503

#ifndef lua501_equal // already defined when compatibility is active in luaconf.h
inline int lua501_equal(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPEQ);
}
#endif // lua501_equal
#ifndef lua_lessthan // already defined when compatibility is active in luaconf.h
inline int lua_lessthan(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPLT);
}
#endif // lua_lessthan
inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const funcs_[])
{
    luaL_setfuncs(L_, funcs_, 0);
}
inline int lua504_dump(lua_State* L_, lua_Writer writer_, void* data_, int strip_)
{
    return lua_dump(L_, writer_, data_, strip_);
}
inline int luaL_optint(lua_State* L_, int n_, lua_Integer d_)
{
    return static_cast<int>(luaL_optinteger(L_, n_, d_));
}

#endif // LUA_VERSION_NUM == 503

// #################################################################################################

#if LUA_VERSION_NUM < 504

void* lua_newuserdatauv(lua_State* L_, size_t sz_, int nuvalue_);
int lua_getiuservalue(lua_State* L_, int idx_, int n_);
int lua_setiuservalue(lua_State* L_, int idx_, int n_);

#endif // LUA_VERSION_NUM < 504

// #################################################################################################

// wrap Lua 5.4 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 504

#ifndef lua501_equal // already defined when compatibility is active in luaconf.h
inline int lua501_equal(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPEQ);
}
#endif // lua501_equal
#ifndef lua_lessthan // already defined when compatibility is active in luaconf.h
inline int lua_lessthan(lua_State* L_, int a_, int b_)
{
    return lua_compare(L_, a_, b_, LUA_OPLT);
}
#endif // lua_lessthan
inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const funcs_[])
{
    luaL_setfuncs(L_, funcs_, 0);
}
inline int lua504_dump(lua_State* L_, lua_Writer writer_, void* data_, int strip_)
{
    return lua_dump(L_, writer_, data_, strip_);
}
inline int luaL_optint(lua_State* L_, int n_, lua_Integer d_)
{
    return static_cast<int>(luaL_optinteger(L_, n_, d_));
}
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.4, we don't care about the actual value

#endif // LUA_VERSION_NUM == 504

// #################################################################################################

// a wrapper over lua types to see them easier in a debugger
enum class LuaType
{
    NONE = LUA_TNONE,
    NIL = LUA_TNIL,
    BOOLEAN = LUA_TBOOLEAN,
    LIGHTUSERDATA = LUA_TLIGHTUSERDATA,
    NUMBER = LUA_TNUMBER,
    STRING = LUA_TSTRING,
    TABLE = LUA_TTABLE,
    FUNCTION = LUA_TFUNCTION,
    USERDATA = LUA_TUSERDATA,
    THREAD = LUA_TTHREAD,
    CDATA = 10 // LuaJIT CDATA
};

inline LuaType lua_type_as_enum(lua_State* L_, int idx_)
{
    return static_cast<LuaType>(lua_type(L_, idx_));
}
inline char const* lua_typename(lua_State* L_, LuaType t_)
{
    return lua_typename(L_, static_cast<int>(t_));
}

LuaType luaG_getmodule(lua_State* L_, char const* name_);
