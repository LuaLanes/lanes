#pragma once

#include "debug.h"
#include "luaerrors.h"

using namespace std::chrono_literals;

// #################################################################################################

inline void STACK_GROW(lua_State* L_, int n_)
{
    if (!lua_checkstack(L_, n_)) {
        raise_luaL_error(L_, "Cannot grow stack!");
    }
}

// #################################################################################################

#define LUAG_FUNC(func_name) int LG_##func_name(lua_State* const L_)

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
    using type = T;
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
    using type = T;
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
