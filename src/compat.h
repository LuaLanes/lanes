#pragma once

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

// #################################################################################################
// #################################################################################################
// All the compatibility wrappers we expose start with luaG_
// #################################################################################################
// #################################################################################################

// use this in place of lua_absindex to save a function call
inline int luaG_absindex(lua_State* L_, int idx_)
{
    return (((idx_) >= 0 || (idx_) <= LUA_REGISTRYINDEX) ? (idx_) : lua_gettop(L_) + (idx_) + 1);
}

// #################################################################################################

template <typename LUA_DUMP>
concept RequiresOldLuaDump = requires(LUA_DUMP f_) { { f_(nullptr, nullptr, nullptr) } -> std::same_as<int>; };

template <RequiresOldLuaDump LUA_DUMP>
static inline int WrapLuaDump(LUA_DUMP f_, lua_State* const L_, lua_Writer const writer_, void* const data_, [[maybe_unused]] int const strip_)
{
    return f_(L_, writer_, data_);
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_DUMP>
concept RequiresNewLuaDump = requires(LUA_DUMP f_) { { f_(nullptr, nullptr, nullptr, 0) } -> std::same_as<int>; };

template <RequiresNewLuaDump LUA_DUMP>
static inline int WrapLuaDump(LUA_DUMP f_, lua_State* const L_, lua_Writer const writer_, void* const data_, int const strip_)
{
    return f_(L_, writer_, data_, strip_);
}

// -------------------------------------------------------------------------------------------------

static inline int luaG_dump(lua_State* const L_, lua_Writer const writer_, void* const data_, int const strip_)
{
    return WrapLuaDump(lua_dump, L_, writer_, data_, strip_);
}

// #################################################################################################

int luaG_getalluservalues(lua_State* L_, int idx_);

// #################################################################################################

template <typename LUA_GETFIELD>
concept RequiresOldLuaGetfield = requires(LUA_GETFIELD f_)
{
    {
        f_(nullptr, 0, nullptr)
    } -> std::same_as<void>;
};

template <RequiresOldLuaGetfield LUA_GETFIELD>
static inline int WrapLuaGetField(LUA_GETFIELD f_, lua_State* const L_, int const idx_, char const* const name_)
{
    f_(L_, idx_, name_);
    return lua_type(L_, -1);
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_GETFIELD>
concept RequiresNewLuaGetfield = requires(LUA_GETFIELD f_)
{
    {
        f_(nullptr, 0, nullptr)
    } -> std::same_as<int>;
};

template <RequiresNewLuaGetfield LUA_GETFIELD>
static inline int WrapLuaGetField(LUA_GETFIELD f_, lua_State* const L_, int const idx_, char const* const name_)
{
    return f_(L_, idx_, name_);
}

// -------------------------------------------------------------------------------------------------

static inline LuaType luaG_getfield(lua_State* const L_, int const idx_, std::string_view const& name_)
{
    return static_cast<LuaType>(WrapLuaGetField(lua_getfield, L_, idx_, name_.data()));
}

// #################################################################################################

LuaType luaG_getmodule(lua_State* L_, std::string_view const& name_);

// #################################################################################################

inline void luaG_registerlibfuncs(lua_State* L_, luaL_Reg const* funcs_)
{
    // fake externs to make clang happy...
    extern void luaL_register(lua_State*, char const*, luaL_Reg const*); // Lua 5.1
    extern void luaL_setfuncs(lua_State* const L_, luaL_Reg const funcs_[], int nup_); // Lua 5.2+
    if constexpr (LUA_VERSION_NUM == 501) {
        luaL_register(L_, nullptr, funcs_);
    } else {
        luaL_setfuncs(L_, funcs_, 0);
    }
}

// #################################################################################################

template <typename LUA_RESUME>
concept RequiresLuaResume51 = requires(LUA_RESUME f_) { { f_(nullptr, 0) } -> std::same_as<int>; };

template <RequiresLuaResume51 LUA_RESUME>
static inline int WrapLuaResume(LUA_RESUME const f_, lua_State* const L_, [[maybe_unused]] lua_State* const from_, int const nargs_, int* const nresults_)
{
    int const _resultsStart{ lua_gettop(L_) - nargs_ - 1 };
    int const _rc{ f_(L_, nargs_) };
    *nresults_ = lua_gettop(L_) - _resultsStart;
    return _rc;
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_RESUME>
concept RequiresLuaResume52 = requires(LUA_RESUME f_) { { f_(nullptr, nullptr, 0) } -> std::same_as<int>; };

template <RequiresLuaResume52 LUA_RESUME>
static inline int WrapLuaResume(LUA_RESUME const f_, lua_State* const L_, lua_State* const from_, int const nargs_, [[maybe_unused]] int* const nresults_)
{
    int const _resultsStart{ lua_gettop(L_) - nargs_ - 1 };
    int const _rc{ f_(L_, from_, nargs_) };
    *nresults_ = lua_gettop(L_) - _resultsStart;
    return _rc;
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_RESUME>
concept RequiresLuaResume54 = requires(LUA_RESUME f_) { { f_(nullptr, nullptr, 0, nullptr) } -> std::same_as<int>; };

template <RequiresLuaResume54 LUA_RESUME>
static inline int WrapLuaResume(LUA_RESUME const f_, lua_State* const L_, lua_State* const from_, int const nargs_, int* const nresults_)
{
    return f_(L_, from_, nargs_, nresults_);
}

// -------------------------------------------------------------------------------------------------

static inline LuaError luaG_resume(lua_State* const L_, lua_State* const from_, int const nargs_, int* const nresults_)
{
    return ToLuaError(WrapLuaResume(lua_resume, L_, from_, nargs_, nresults_));
}

// #################################################################################################

template <size_t N>
static inline void luaG_newlib(lua_State* const L_, luaL_Reg const (&funcs_)[N])
{
    lua_createtable(L_, 0, N - 1);
    luaG_registerlibfuncs(L_, funcs_);
}

// #################################################################################################

template <typename T>
[[nodiscard]] T* luaG_newuserdatauv(lua_State* L_, int nuvalue_)
{
    return static_cast<T*>(lua_newuserdatauv(L_, sizeof(T), nuvalue_));
}

// #################################################################################################

inline void luaG_pushglobaltable(lua_State* const L_)
{
#ifdef LUA_GLOBALSINDEX // All flavors of Lua 5.1
    ::lua_pushvalue(L_, LUA_GLOBALSINDEX);
#else // LUA_GLOBALSINDEX
    ::lua_rawgeti(L_, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
#endif // LUA_GLOBALSINDEX
}

// #################################################################################################

inline void luaG_setfield(lua_State* const L_, int const idx_, char const* k_) = delete;
inline void luaG_setfield(lua_State* const L_, int const idx_, std::string_view const& k_)
{
    lua_setfield(L_, idx_, k_.data());
}

// #################################################################################################

inline void luaG_setmetatable(lua_State* const L_, std::string_view const& tname_)
{
    // fake externs to make clang happy...
    extern void luaL_setmetatable(lua_State* const L_, char const* const tname_); // Lua 5.2+
    if constexpr (LUA_VERSION_NUM == 501) {
        luaL_setmetatable(L_, tname_.data());
    } else {
        luaL_getmetatable(L_, tname_.data());
        lua_setmetatable(L_, -2);
    }
}

// #################################################################################################

// a small helper to extract a full userdata pointer from the stack in a safe way
template <typename T>
[[nodiscard]] T* luaG_tofulluserdata(lua_State* const L_, int const index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_type(L_, index_) == LUA_TUSERDATA);
    return static_cast<T*>(lua_touserdata(L_, index_));
}

// -------------------------------------------------------------------------------------------------

template <typename T>
[[nodiscard]] auto luaG_tolightuserdata(lua_State* const L_, int const index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_islightuserdata(L_, index_));
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<T>(lua_touserdata(L_, index_));
    } else {
        return static_cast<T*>(lua_touserdata(L_, index_));
    }
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::string_view luaG_typename(lua_State* const L_, LuaType const t_)
{
    return lua_typename(L_, static_cast<int>(t_));
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::string_view luaG_typename(lua_State* const L_, int const idx_)
{
    return luaG_typename(L_, luaG_type(L_, idx_));
}

// #################################################################################################

// must keep as a macro as long as we do constant string concatenations
#define STRINGVIEW_FMT "%.*s"

// a replacement of lua_tolstring
[[nodiscard]] inline std::string_view luaG_tostring(lua_State* const L_, int const idx_)
{
    size_t _len{ 0 };
    char const* _str{ lua_tolstring(L_, idx_, &_len) };
    return _str ? std::string_view{ _str, _len } : "";
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
inline std::string_view luaG_pushstring(lua_State* const L_, std::string_view const& str_, EXTRA&&... extra_)
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
