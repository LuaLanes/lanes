#pragma once

#include "debug.h"
#include "luaerrors.h"
#include "unique.hpp"

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

DECLARE_UNIQUE_TYPE(SourceState, lua_State*);
DECLARE_UNIQUE_TYPE(DestState, lua_State*);

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
