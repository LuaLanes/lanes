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

#include <cassert>
#include <chrono>
#include <tuple>
#include <type_traits>

using namespace std::chrono_literals;

// #################################################################################################

// use this instead of Lua's lua_error
[[noreturn]] static inline void raise_lua_error(lua_State* const L_)
{
    std::ignore = lua_error(L_); // doesn't return
    assert(false); // we should never get here, but i'm paranoid
}

// #################################################################################################

// use this instead of Lua's luaL_error
template <typename... ARGS>
[[noreturn]] static inline void raise_luaL_error(lua_State* const L_, std::string_view const& fmt_, ARGS... args_)
{
    std::ignore = luaL_error(L_, fmt_.data(), std::forward<ARGS>(args_)...); // doesn't return
    assert(false); // we should never get here, but i'm paranoid
}

// #################################################################################################

// use this instead of Lua's luaL_argerror
template <typename... ARGS>
[[noreturn]] static inline void raise_luaL_argerror(lua_State* const L_, int const arg_, std::string_view const& extramsg_)
{
    std::ignore = luaL_argerror(L_, arg_, extramsg_.data()); // doesn't return
    assert(false); // we should never get here, but i'm paranoid
}

// #################################################################################################

#if LUA_VERSION_NUM >= 504
// use this instead of Lua's luaL_typeerror
template <typename... ARGS>
[[noreturn]] static inline void raise_luaL_typeerror(lua_State* const L_, int const arg_, std::string_view const& tname_)
{
    std::ignore = luaL_typeerror(L_, arg_, tname_.data()); // doesn't return
    assert(false); // we should never get here, but i'm paranoid
}
#endif // LUA_VERSION_NUM

// #################################################################################################

#ifdef NDEBUG

#define LUA_ASSERT(L_, c) ; // nothing

#define STACK_CHECK_START_REL(L_, offset_)
#define STACK_CHECK_START_ABS(L_, offset_)
#define STACK_CHECK_RESET_REL(L_, offset_)
#define STACK_CHECK_RESET_ABS(L_, offset_)
#define STACK_CHECK(L_, offset_)

#else // NDEBUG

inline void LUA_ASSERT_IMPL(lua_State* L_, bool cond_, char const* file_, int const line_, char const* txt_)
{
    if (!cond_) {
        raise_luaL_error(L_, "LUA_ASSERT %s:%d '%s'", file_, line_, txt_);
    }
}

#define LUA_ASSERT(L_, cond_) LUA_ASSERT_IMPL(L_, cond_, __FILE__, __LINE__, #cond_)

class StackChecker
{
    private:
    lua_State* const L;
    int oldtop;

    public:
    struct Relative
    {
        int const offset;

        operator int() const { return offset; }
    };

    struct Absolute
    {
        int const offset;

        operator int() const { return offset; }
    };

    StackChecker(lua_State* const L_, Relative offset_, char const* file_, size_t const line_)
    : L{ L_ }
    , oldtop{ lua_gettop(L_) - offset_ }
    {
        if ((offset_ < 0) || (oldtop < 0)) {
            assert(false);
            raise_luaL_error(L, "STACK INIT ASSERT failed (%d not %d): %s:%llu", lua_gettop(L), offset_, file_, line_);
        }
    }

    StackChecker(lua_State* const L_, Absolute pos_, char const* file_, size_t const line_)
    : L{ L_ }
    , oldtop{ 0 }
    {
        if (lua_gettop(L) != pos_) {
            assert(false);
            raise_luaL_error(L, "STACK INIT ASSERT failed (%d not %d): %s:%llu", lua_gettop(L), pos_, file_, line_);
        }
    }

    StackChecker& operator=(StackChecker const& rhs_)
    {
        assert(L == rhs_.L);
        oldtop = rhs_.oldtop;
        return *this;
    }

    // verify if the distance between the current top and the initial one is what we expect
    void check(int expected_, char const* file_, size_t const line_)
    {
        if (expected_ != LUA_MULTRET) {
            int const actual{ lua_gettop(L) - oldtop };
            if (actual != expected_) {
                assert(false);
                raise_luaL_error(L, "STACK ASSERT failed (%d not %d): %s:%llu", actual, expected_, file_, line_);
            }
        }
    }
};

#define STACK_CHECK_START_REL(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_START_ABS(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_RESET_REL(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_RESET_ABS(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK(L, offset_) _stackChecker_##L.check(offset_, __FILE__, __LINE__)

#endif // NDEBUG

// #################################################################################################

inline void STACK_GROW(lua_State* L_, int n_)
{
    if (!lua_checkstack(L_, n_)) {
        raise_luaL_error(L_, "Cannot grow stack!");
    }
}

// #################################################################################################

#define LUAG_FUNC(func_name) [[nodiscard]] int LG_##func_name(lua_State* const L_)

// #################################################################################################

// a small helper to extract a full userdata pointer from the stack in a safe way
template <typename T>
[[nodiscard]] T* lua_tofulluserdata(lua_State* L_, int index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_type(L_, index_) == LUA_TUSERDATA);
    return static_cast<T*>(lua_touserdata(L_, index_));
}

template <typename T>
[[nodiscard]] auto lua_tolightuserdata(lua_State* L_, int index_)
{
    LUA_ASSERT(L_, lua_isnil(L_, index_) || lua_islightuserdata(L_, index_));
    if constexpr (std::is_pointer_v<T>) {
        return static_cast<T>(lua_touserdata(L_, index_));
    } else {
        return static_cast<T*>(lua_touserdata(L_, index_));
    }
}

template <typename T>
[[nodiscard]] T* lua_newuserdatauv(lua_State* L_, int nuvalue_)
{
    return static_cast<T*>(lua_newuserdatauv(L_, sizeof(T), nuvalue_));
}

// #################################################################################################

using lua_Duration = std::chrono::template duration<lua_Number>;

// #################################################################################################

// A unique type generator
template <typename T, auto = [] {}, typename specialization = void>
class Unique
{
    private:
    T val;

    public:
    Unique() = default;
    operator T() const { return val; }
    explicit Unique(T b_)
    : val{ b_ }
    {
    }
};

template <typename T, auto lambda>
class Unique<T, lambda, std::enable_if_t<!std::is_scalar_v<T>>>
: public T
{
    public:
    using T::T;
    explicit Unique(T const& b_)
    : T{ b_ }
    {
    }
};

// #################################################################################################

using SourceState = Unique<lua_State*>;
using DestState = Unique<lua_State*>;

// #################################################################################################

// A helper to issue an error if the provided optional doesn't contain a value
// we can't use std::optional::value_or(luaL_error(...)), because the 'or' value is always evaluated
template <typename T>
concept IsOptional = requires(T x)
{
    x.value_or(T{});
};

template <typename T, typename... Ts>
requires IsOptional<T>
typename T::value_type const& OptionalValue(T const& x_, Ts... args_)
{
    if (!x_.has_value()) {
        raise_luaL_error(std::forward<Ts>(args_)...);
    }
    return x_.value();
}
