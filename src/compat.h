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

#include "debug.h"

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

#ifndef LUA_OK
#define LUA_OK 0 // doesn't exist in Lua 5.1
#endif // LUA_OK

#ifndef LUA_ERRGCMM
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.1 and Lua 5.4, we don't care about the actual value
#endif // LUA_ERRGCMM


#ifndef LUA_LOADED_TABLE
#define LUA_LOADED_TABLE "_LOADED" // doesn't exist before Lua 5.3
#endif // LUA_LOADED_TABLE

// code is now preferring Lua 5.4 API

// #################################################################################################

// a strong-typed wrapper over lua types to see them easier in a debugger
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

// #################################################################################################

// add some Lua 5.3-style API when building for Lua 5.1
#if LUA_VERSION_NUM == 501

inline size_t lua_rawlen(lua_State* L_, int idx_)
{
    return lua_objlen(L_, idx_);
}
void luaL_requiref(lua_State* L_, const char* modname_, lua_CFunction openf_, int glb_); // implementation copied from Lua 5.2 sources

int luaL_getsubtable(lua_State* L_, int idx_, const char* fname_);

#endif // LUA_VERSION_NUM == 501

// #################################################################################################

// wrap Lua 5.3 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 503

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

#define LUA_GNAME "_G"

#endif // LUA_VERSION_NUM < 504

// #################################################################################################

// wrap Lua 5.4 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 504

inline int luaL_optint(lua_State* L_, int n_, lua_Integer d_)
{
    return static_cast<int>(luaL_optinteger(L_, n_, d_));
}

#endif // LUA_VERSION_NUM == 504

// #################################################################################################

// a strong-typed wrapper over lua error codes to see them easier in a debugger
enum class LuaError
{
    OK = LUA_OK,
    YIELD = LUA_YIELD,
    ERRRUN = LUA_ERRRUN,
    ERRSYNTAX = LUA_ERRSYNTAX,
    ERRMEM = LUA_ERRMEM,
    ERRGCMM = LUA_ERRGCMM,
    ERRERR = LUA_ERRERR,
    ERRFILE = LUA_ERRFILE
};

inline constexpr LuaError ToLuaError(int const rc_)
{
    assert(rc_ == LUA_OK || rc_ == LUA_YIELD || rc_ == LUA_ERRRUN || rc_ == LUA_ERRSYNTAX || rc_ == LUA_ERRMEM || rc_ == LUA_ERRGCMM || rc_ == LUA_ERRERR || rc_ == LUA_ERRFILE);
    return static_cast<LuaError>(rc_);
}

// #################################################################################################

// break lexical order for that one because it's needed below
inline LuaType luaG_type(lua_State* const L_, int const idx_)
{
    return static_cast<LuaType>(lua_type(L_, idx_));
}

// -------------------------------------------------------------------------------------------------

// Default matches Lua 5.4 as of now
template <int VERSION, typename SPECIALIZE = void>
struct Wrap
{
    static inline int lua_dump(lua_State* const L_, lua_Writer const writer_, void* const data_, int const strip_)
    {
        return ::lua_dump(L_, writer_, data_, strip_);
    }

    static inline LuaType lua_getfield(lua_State* L_, int idx_, std::string_view const& k_)
    {
        // starting with Lua 5.3, lua_getfield returns the type of the pushed value
        return static_cast<LuaType>(::lua_getfield(L_, idx_, k_.data()));
    }

    template <size_t N>
    static inline void (luaL_newlib)(lua_State* const L_, luaL_Reg const (&funcs_)[N])
    {
        lua_createtable(L_, 0, N - 1);
        ::luaL_setfuncs(L_, funcs_, 0);
    }

    static void luaL_setfuncs(lua_State* const L_, luaL_Reg const funcs_[], int nup_)
    {
        ::luaL_setfuncs(L_, funcs_, nup_);
    }

    static void luaL_setmetatable(lua_State* const L_, std::string_view const& tname_)
    {
        ::luaL_setmetatable(L_, tname_.data());
    }
};

// #################################################################################################

template <int VERSION>
struct Wrap<VERSION, typename std::enable_if<VERSION == 503>::type>
{
    static inline int lua_dump(lua_State* L_, lua_Writer writer_, void* data_, int strip_)
    {
        return ::lua_dump(L_, writer_, data_, strip_);
    }

    static inline LuaType lua_getfield(lua_State* L_, int idx_, std::string_view const& k_)
    {
        // starting with Lua 5.3, lua_getfield returns the type of the pushed value
        return static_cast<LuaType>(::lua_getfield(L_, idx_, k_.data()));
    }

    template <size_t N>
    static inline void (luaL_newlib)(lua_State* const L_, luaL_Reg const (&funcs_)[N])
    {
        lua_createtable(L_, 0, N - 1);
        ::luaL_setfuncs(L_, funcs_, 0);
    }

    static void luaL_setfuncs(lua_State* const L_, luaL_Reg const funcs_[], int const nup_)
    {
        ::luaL_setfuncs(L_, funcs_, nup_);
    }

    static void luaL_setmetatable(lua_State* const L_, std::string_view const& tname_)
    {
        ::luaL_setmetatable(L_, tname_.data());
    }
};

// #################################################################################################

template <int VERSION>
struct Wrap<VERSION, typename std::enable_if<VERSION == 502>::type>
{
    static inline int lua_dump(lua_State* const L_, lua_Writer const writer_, void* const data_, [[maybe_unused]] int const strip_)
    {
        return ::lua_dump(L_, writer_, data_);
    }

    static inline LuaType lua_getfield(lua_State* L_, int idx_, std::string_view const& k_)
    {
        // before Lua 5.3, lua_getfield returns nothing
        ::lua_getfield(L_, idx_, k_.data());
        return luaG_type(L_, -1);
    }

    template <size_t N>
    static inline void (luaL_newlib)(lua_State* const L_, luaL_Reg const (&funcs_)[N])
    {
        ::lua_createtable(L_, 0, N - 1);
        ::luaL_setfuncs(L_, funcs_, 0);
    }

    static void luaL_setfuncs(lua_State* const L_, luaL_Reg const funcs_[], int const nup_)
    {
        ::luaL_setfuncs(L_, funcs_, nup_);
    }

    static void luaL_setmetatable(lua_State* const L_, std::string_view const& tname_)
    {
        ::luaL_setmetatable(L_, tname_.data());
    }
};

// #################################################################################################

template <int VERSION>
struct Wrap<VERSION, typename std::enable_if<VERSION == 501>::type>
{
    static inline int lua_dump(lua_State* const L_, lua_Writer const writer_, void* const data_, [[maybe_unused]] int const strip_)
    {
        return ::lua_dump(L_, writer_, data_);
    }

    static inline LuaType lua_getfield(lua_State* L_, int idx_, std::string_view const& k_)
    {
        // before Lua 5.3, lua_getfield returns nothing
        ::lua_getfield(L_, idx_, k_.data());
        return luaG_type(L_, -1);
    }

    template<size_t N>
    static inline void (luaL_newlib)(lua_State* const L_, luaL_Reg const (&funcs_)[N])
    {
        lua_createtable(L_, 0, N - 1);
        ::luaL_register(L_, nullptr, funcs_);
    }

    static void luaL_setfuncs(lua_State* const L_, luaL_Reg const funcs_[], [[maybe_unused]] int const nup_)
    {
        ::luaL_register(L_, nullptr, funcs_);
    }

    static void luaL_setmetatable(lua_State* const L_, std::string_view const& tname_)
    {
        luaL_getmetatable(L_, tname_.data());
        lua_setmetatable(L_, -2);
    }
};

// #################################################################################################
// All the compatibility wrappers we expose start with luaG_

// -------------------------------------------------------------------------------------------------

// use this in place of lua_absindex to save a function call
inline int luaG_absindex(lua_State* L_, int idx_)
{
    return (((idx_) >= 0 || (idx_) <= LUA_REGISTRYINDEX) ? (idx_) : lua_gettop(L_) + (idx_) + 1);
}

// -------------------------------------------------------------------------------------------------

inline int luaG_dump(lua_State* L_, lua_Writer writer_, void* data_, int strip_)
{
    return Wrap<LUA_VERSION_NUM>::lua_dump(L_, writer_, data_, strip_);
}

// -------------------------------------------------------------------------------------------------

int luaG_getalluservalues(lua_State* L_, int idx_);

// -------------------------------------------------------------------------------------------------

[[nodiscard]] inline LuaType luaG_getfield(lua_State* L_, int idx_, std::string_view const& k_)
{
    return Wrap<LUA_VERSION_NUM>::lua_getfield(L_, idx_, k_);
}

// -------------------------------------------------------------------------------------------------

LuaType luaG_getmodule(lua_State* L_, std::string_view const& name_);

// -------------------------------------------------------------------------------------------------

template<size_t N>
inline void luaG_newlib(lua_State* const L_, luaL_Reg const (&funcs_)[N])
{
    (Wrap<LUA_VERSION_NUM>::luaL_newlib)(L_, funcs_);
}

// -------------------------------------------------------------------------------------------------

template <typename T>
[[nodiscard]] T* luaG_newuserdatauv(lua_State* L_, int nuvalue_)
{
    return static_cast<T*>(lua_newuserdatauv(L_, sizeof(T), nuvalue_));
}

// -------------------------------------------------------------------------------------------------

inline void luaG_pushglobaltable(lua_State* const L_)
{
#ifdef LUA_GLOBALSINDEX // All flavors of Lua 5.1
    ::lua_pushvalue(L_, LUA_GLOBALSINDEX);
#else // LUA_GLOBALSINDEX
    ::lua_rawgeti(L_, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
#endif // LUA_GLOBALSINDEX
}

// -------------------------------------------------------------------------------------------------

inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const funcs_[])
{
    Wrap<LUA_VERSION_NUM>::luaL_setfuncs(L_, funcs_, 0);
}

// -------------------------------------------------------------------------------------------------

inline void luaG_setmetatable(lua_State* const L_, std::string_view const& tname_)
{
    return Wrap<LUA_VERSION_NUM>::luaL_setmetatable(L_, tname_);
}

// -------------------------------------------------------------------------------------------------

// a small helper to extract a full userdata pointer from the stack in a safe way
template <typename T>
[[nodiscard]] T* luaG_tofulluserdata(lua_State* L_, int index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_type(L_, index_) == LUA_TUSERDATA);
    return static_cast<T*>(lua_touserdata(L_, index_));
}

// -------------------------------------------------------------------------------------------------

template <typename T>
[[nodiscard]] auto luaG_tolightuserdata(lua_State* L_, int index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_islightuserdata(L_, index_));
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<T>(lua_touserdata(L_, index_));
    } else {
        return static_cast<T*>(lua_touserdata(L_, index_));
    }
}

// -------------------------------------------------------------------------------------------------

inline char const* luaG_typename(lua_State* L_, LuaType t_)
{
    return lua_typename(L_, static_cast<int>(t_));
}

// #################################################################################################

// must keep as a macro as long as we do constant string concatenations
#define STRINGVIEW_FMT "%.*s"

// a replacement of lua_tolstring
[[nodiscard]] inline std::string_view luaG_tostring(lua_State* const L_, int const idx_)
{
    size_t _len{ 0 };
    char const* _str{ lua_tolstring(L_, idx_, &_len) };
    return std::string_view{ _str, _len };
}

[[nodiscard]] inline std::string_view luaG_checkstring(lua_State* const L_, int const idx_)
{
    size_t _len{ 0 };
    char const* _str{ luaL_checklstring(L_, idx_, &_len) };
    return std::string_view{ _str, _len };
}

[[nodiscard]] inline std::string_view luaG_optstring(lua_State* const L_, int const idx_, std::string_view const& default_)
{
    if (lua_isnoneornil(L_, idx_)) {
        return default_;
    }
    size_t _len{ 0 };
    char const* _str{ luaL_optlstring(L_, idx_, default_.data(), &_len) };
    return std::string_view{ _str, _len };
}

template<typename ...EXTRA>
[[nodiscard]] inline std::string_view luaG_pushstring(lua_State* const L_, std::string_view const& str_, EXTRA&&... extra_)
{
    if constexpr (sizeof...(EXTRA) == 0) {
        if constexpr (LUA_VERSION_NUM == 501) {
            // lua_pushlstring doesn't return a value in Lua 5.1
            lua_pushlstring(L_, str_.data(), str_.size());
            return luaG_tostring(L_, -1);
        } else {
            return std::string_view{ lua_pushlstring(L_, str_.data(), str_.size()), str_.size() };
        }
    } else {
        static_assert((... && std::is_trivial_v<std::decay_t<EXTRA>>));
        return std::string_view{ lua_pushfstring(L_, str_.data(), std::forward<EXTRA>(extra_)...) };
    }
}
