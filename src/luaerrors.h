#pragma once

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
