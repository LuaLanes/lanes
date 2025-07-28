#pragma once

#include "debug.hpp"
#include "stackindex.hpp"

// try to detect if we are building against LuaJIT or MoonJIT
#if defined(LUA_JITLIBNAME)
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
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.1 and Lua 5.4/5.5, we don't care about the actual value
#endif // LUA_ERRGCMM


#ifndef LUA_LOADED_TABLE
#define LUA_LOADED_TABLE "_LOADED" // doesn't exist before Lua 5.3
#endif // LUA_LOADED_TABLE

// code is now preferring Lua 5.5 API

// #################################################################################################

// a strong-typed wrapper over lua types to see them easier in a debugger
enum class [[nodiscard]] LuaType
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

enum class [[nodiscard]] LuaHookMask
{
    None = 0,
    Call = LUA_MASKCALL,
    Ret = LUA_MASKRET,
    Line = LUA_MASKLINE,
    Count = LUA_MASKCOUNT,
    All = LUA_MASKCALL | LUA_MASKRET | LUA_MASKLINE | LUA_MASKCOUNT
};

// #################################################################################################

// add some Lua 5.3-style API when building for Lua 5.1
#if LUA_VERSION_NUM == 501

inline size_t lua_rawlen(lua_State* L_, StackIndex idx_)
{
    return lua_objlen(L_, idx_);
}
void luaL_requiref(lua_State* L_, char const* modname_, lua_CFunction openf_, int glb_); // implementation copied from Lua 5.2 sources

int luaL_getsubtable(lua_State* L_, StackIndex idx_, char const* fname_);

#endif // LUA_VERSION_NUM == 501

// #################################################################################################

#if LUA_VERSION_NUM < 504

void* lua_newuserdatauv(lua_State* L_, size_t sz_, UserValueCount nuvalue_);
int lua_getiuservalue(lua_State* L_, StackIndex idx_, UserValueIndex n_);
int lua_setiuservalue(lua_State* L_, StackIndex idx_, UserValueIndex n_);

#define LUA_GNAME "_G"

#endif // LUA_VERSION_NUM < 504

// #################################################################################################

#if LUA_VERSION_NUM < 505

unsigned int luaL_makeseed(lua_State*);

#endif // LUA_VERSION_NUM < 505

// #################################################################################################

// a strong-typed wrapper over lua error codes to see them easier in a debugger
enum class [[nodiscard]] LuaError
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

[[nodiscard]]
inline constexpr LuaError ToLuaError(int const rc_)
{
    assert(rc_ == LUA_OK || rc_ == LUA_YIELD || rc_ == LUA_ERRRUN || rc_ == LUA_ERRSYNTAX || rc_ == LUA_ERRMEM || rc_ == LUA_ERRGCMM || rc_ == LUA_ERRERR || rc_ == LUA_ERRFILE);
    return static_cast<LuaError>(rc_);
}

// #################################################################################################

// break lexical order for that one because it's needed below
[[nodiscard]]
inline LuaType luaW_type(lua_State* const L_, StackIndex const idx_)
{
    return static_cast<LuaType>(lua_type(L_, idx_));
}

// #################################################################################################
// #################################################################################################
// All the compatibility wrappers we expose start with luaW_
// #################################################################################################
// #################################################################################################

// must keep as a macro as long as we do constant string concatenations
#define STRINGVIEW_FMT "%.*s"

// a replacement of lua_tolstring
[[nodiscard]]
inline std::string_view luaW_tostring(lua_State* const L_, StackIndex const idx_)
{
    size_t _len{ 0 };
    char const* _str{ lua_tolstring(L_, idx_, &_len) };
    return _str ? std::string_view{ _str, _len } : "";
}

[[nodiscard]]
inline std::string_view luaW_checkstring(lua_State* const L_, StackIndex const idx_)
{
    size_t _len{ 0 };
    char const* _str{ luaL_checklstring(L_, idx_, &_len) };
    return std::string_view{ _str, _len };
}

[[nodiscard]]
inline std::string_view luaW_optstring(lua_State* const L_, StackIndex const idx_, std::string_view const& default_)
{
    if (lua_isnoneornil(L_, idx_)) {
        return default_;
    }
    size_t _len{ 0 };
    char const* _str{ luaL_optlstring(L_, idx_, default_.data(), &_len) };
    return std::string_view{ _str, _len };
}

template <typename... EXTRA>
inline std::string_view luaW_pushstring(lua_State* const L_, std::string_view const& str_, EXTRA&&... extra_)
{
    if constexpr (sizeof...(EXTRA) == 0) {
        if constexpr (LUA_VERSION_NUM == 501) {
            // lua_pushlstring doesn't return a value in Lua 5.1
            lua_pushlstring(L_, str_.data(), str_.size());
            return luaW_tostring(L_, kIdxTop);
        } else {
            return std::string_view{ lua_pushlstring(L_, str_.data(), str_.size()), str_.size() };
        }
    } else {
        static_assert((... && std::is_trivial_v<std::decay_t<EXTRA>>));
        return std::string_view{ lua_pushfstring(L_, str_.data(), std::forward<EXTRA>(extra_)...) };
    }
}

// #################################################################################################

// use this in place of lua_absindex to save a function call
inline StackIndex luaW_absindex(lua_State* const L_, StackIndex const idx_)
{
    return StackIndex{ (idx_ >= 0 || idx_ <= kIdxRegistry) ? idx_ : StackIndex{ lua_gettop(L_) + idx_ + 1 } };
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

static inline int luaW_dump(lua_State* const L_, lua_Writer const writer_, void* const data_, int const strip_)
{
    return WrapLuaDump(lua_dump, L_, writer_, data_, strip_);
}

// #################################################################################################

UserValueCount luaW_getalluservalues(lua_State* L_, StackIndex idx_);

// #################################################################################################

template <typename LUA_GETFIELD>
concept RequiresOldLuaGetfield = requires(LUA_GETFIELD f_)
{
    {
        f_(nullptr, 0, nullptr)
    } -> std::same_as<void>;
};

template <RequiresOldLuaGetfield LUA_GETFIELD>
static inline int WrapLuaGetField(LUA_GETFIELD f_, lua_State* const L_, StackIndex const idx_, std::string_view const& name_)
{
    f_(L_, idx_, name_.data());
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
static inline int WrapLuaGetField(LUA_GETFIELD f_, lua_State* const L_, StackIndex const idx_, std::string_view const& name_)
{
    return f_(L_, idx_, name_.data());
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]]
static inline LuaType luaW_getfield(lua_State* const L_, StackIndex const idx_, std::string_view const& name_)
{
    return static_cast<LuaType>(WrapLuaGetField(lua_getfield, L_, idx_, name_));
}

// #################################################################################################

[[nodiscard]]
LuaType luaW_getmodule(lua_State* L_, std::string_view const& name_);

// #################################################################################################

template <typename LUA_NEWSTATE>
concept RequiresOldLuaNewState = requires(LUA_NEWSTATE f_)
{
    {
        f_(nullptr, 0)
    } -> std::same_as<lua_State*>;
};

template <RequiresOldLuaNewState LUA_NEWSTATE>
static inline lua_State* WrapLuaNewState(LUA_NEWSTATE const lua_newstate_, lua_Alloc const allocf_, void* const ud_, [[maybe_unused]] unsigned int const seed_)
{
    // until Lua 5.5, lua_newstate has only 2 parameters
    return lua_newstate_(allocf_, ud_);
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_NEWSTATE>
concept RequiresNewLuaNewState = requires(LUA_NEWSTATE f_)
{
    {
        f_(nullptr, nullptr, 0)
    } -> std::same_as<lua_State*>;
};

template <RequiresNewLuaNewState LUA_NEWSTATE>
static inline lua_State* WrapLuaNewState(LUA_NEWSTATE const lua_newstate_, lua_Alloc const allocf_, void* const ud_, unsigned int const seed_)
{
    // starting with Lua 5.5, lua_newstate has 3 parameters
    return lua_newstate_(allocf_, ud_, seed_);
}

// -------------------------------------------------------------------------------------------------

static inline lua_State* luaW_newstate(lua_Alloc const allocf_, void* const ud_, unsigned int const seed_)
{
    return WrapLuaNewState(lua_newstate, allocf_, ud_, seed_);
}

// #################################################################################################

template<typename ENUM>
requires std::is_enum_v<ENUM>
[[nodiscard]]
ENUM luaW_optenum(lua_State* const L_, StackIndex const idx_, ENUM const def_)
{
    return static_cast<ENUM>(luaL_optinteger(L_, idx_, static_cast<std::underlying_type_t<ENUM>>(def_)));
}

// #################################################################################################

inline void luaW_registerlibfuncs(lua_State* const L_, luaL_Reg const* funcs_)
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
static inline int WrapLuaResume(LUA_RESUME const lua_resume_, lua_State* const L_, [[maybe_unused]] lua_State* const from_, int const nargs_, int* const nresults_)
{
    // lua_resume is supposed to be called from a "clean" stack:
    // it should only contain the function and its initial arguments on first call, or the resume arguments on subsequent invocations
    int const _rc{ lua_resume_(L_, nargs_) };
    // after resuming, the stack should only contain the yielded values
    *nresults_ = lua_gettop(L_);
    return _rc;
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_RESUME>
concept RequiresLuaResume52 = requires(LUA_RESUME f_) { { f_(nullptr, nullptr, 0) } -> std::same_as<int>; };

template <RequiresLuaResume52 LUA_RESUME>
static inline int WrapLuaResume(LUA_RESUME const lua_resume_, lua_State* const L_, lua_State* const from_, int const nargs_, [[maybe_unused]] int* const nresults_)
{
    // lua_resume is supposed to be called from a "clean" stack:
    // it should only contain the function and its initial arguments on first call, or the resume arguments on subsequent invocations
    int const _rc{ lua_resume_(L_, from_, nargs_) };
    // after resuming, the stack should only contain the yielded values
    *nresults_ = lua_gettop(L_);
    return _rc;
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_RESUME>
concept RequiresLuaResume54 = requires(LUA_RESUME f_) { { f_(nullptr, nullptr, 0, nullptr) } -> std::same_as<int>; };

template <RequiresLuaResume54 LUA_RESUME>
static inline int WrapLuaResume(LUA_RESUME const lua_resume_, lua_State* const L_, lua_State* const from_, int const nargs_, int* const nresults_)
{
    // starting with Lua 5.4, the stack can contain stuff below the actual yielded values, but lua_resume tells us the correct nresult
    return lua_resume_(L_, from_, nargs_, nresults_);
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]]
static inline LuaError luaW_resume(lua_State* const L_, lua_State* const from_, int const nargs_, int* const nresults_)
{
    return ToLuaError(WrapLuaResume(lua_resume, L_, from_, nargs_, nresults_));
}

// #################################################################################################

template <typename LUA_RAWGET>
concept RequiresOldLuaRawget = requires(LUA_RAWGET f_) { { f_(nullptr, 0) } -> std::same_as<void>; };

template <RequiresOldLuaRawget LUA_RAWGET>
static inline LuaType WrapLuaRawget(LUA_RAWGET const lua_rawget_, lua_State* const L_, StackIndex const idx_)
{
    // until Lua 5.3, lua_rawget -> void
    lua_rawget_(L_, idx_);
    return luaW_type(L_, kIdxTop);
}

// -------------------------------------------------------------------------------------------------

template <typename LUA_RAWGET>
concept RequiresNewLuaRawget = requires(LUA_RAWGET f_) { { f_(nullptr, 0) } -> std::same_as<int>; };

template <RequiresNewLuaRawget LUA_RAWGET>
static inline LuaType WrapLuaRawget(LUA_RAWGET const lua_rawget_, lua_State* const L_, StackIndex const idx_)
{
    // starting with Lua 5.3, lua_rawget -> int (the type of the extracted value)
    return static_cast<LuaType>(lua_rawget_(L_, idx_));
}

// -------------------------------------------------------------------------------------------------

static inline LuaType luaW_rawget(lua_State* const L_, StackIndex const idx_)
{
    return WrapLuaRawget(lua_rawget, L_, idx_);
}

// #################################################################################################

static inline LuaType luaW_rawgetfield(lua_State* const L_, StackIndex const idx_, std::string_view const& name_)
{
    auto const _absIdx{ luaW_absindex(L_, idx_) };
    luaW_pushstring(L_, name_);                                                                    // L_: ... t ... name_
    lua_rawget(L_, _absIdx);                                                                       // L_: ... t ... <field>
    return luaW_type(L_, kIdxTop);
}

// #################################################################################################

template <size_t N>
static inline void luaW_newlib(lua_State* const L_, luaL_Reg const (&funcs_)[N])
{
    lua_createtable(L_, 0, N - 1);
    luaW_registerlibfuncs(L_, funcs_);
}

// #################################################################################################

template <typename T>
[[nodiscard]]
T* luaW_newuserdatauv(lua_State* const L_, UserValueCount const nuvalue_)
{
    return static_cast<T*>(lua_newuserdatauv(L_, sizeof(T), nuvalue_));
}

// #################################################################################################

inline void luaW_pushglobaltable(lua_State* const L_)
{
#ifdef LUA_GLOBALSINDEX // All flavors of Lua 5.1
    ::lua_pushvalue(L_, LUA_GLOBALSINDEX);
#else // LUA_GLOBALSINDEX
    ::lua_rawgeti(L_, kIdxRegistry, LUA_RIDX_GLOBALS);
#endif // LUA_GLOBALSINDEX
}

// #################################################################################################

inline void luaW_setfield(lua_State* const L_, StackIndex const idx_, char const* const k_) = delete;
inline void luaW_setfield(lua_State* const L_, StackIndex const idx_, std::string_view const& k_)
{
    lua_setfield(L_, idx_, k_.data());
}

// #################################################################################################

inline void luaW_setmetatable(lua_State* const L_, std::string_view const& tname_)
{
    // fake externs to make clang happy...
    if constexpr (LUA_VERSION_NUM > 501) {
        extern void luaL_setmetatable(lua_State* const L_, char const* const tname_); // Lua 5.2+
        luaL_setmetatable(L_, tname_.data());
    } else {
        luaL_getmetatable(L_, tname_.data());
        lua_setmetatable(L_, -2);
    }
}

// #################################################################################################

// a small helper to extract a full userdata pointer from the stack in a safe way
template <typename T>
[[nodiscard]]
T* luaW_tofulluserdata(lua_State* const L_, StackIndex const index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_type(L_, index_) == LUA_TUSERDATA);
    return static_cast<T*>(lua_touserdata(L_, index_));
}

// -------------------------------------------------------------------------------------------------

template <typename T>
[[nodiscard]]
auto luaW_tolightuserdata(lua_State* const L_, StackIndex const index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_islightuserdata(L_, index_));
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<T>(lua_touserdata(L_, index_));
    } else {
        return static_cast<T*>(lua_touserdata(L_, index_));
    }
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]]
inline std::string_view luaW_typename(lua_State* const L_, LuaType const t_)
{
    return lua_typename(L_, static_cast<int>(t_));
}

// -------------------------------------------------------------------------------------------------

[[nodiscard]]
inline std::string_view luaW_typename(lua_State* const L_, StackIndex const idx_)
{
    return luaW_typename(L_, luaW_type(L_, idx_));
}
