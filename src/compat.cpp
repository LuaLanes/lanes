#include "_pch.hpp"
#include "compat.hpp"

#include "macros_and_utils.hpp"

// #################################################################################################

UserValueCount luaW_getalluservalues(lua_State* const L_, StackIndex const idx_)
{
    STACK_CHECK_START_REL(L_, 0);
    StackIndex const _idx{ luaW_absindex(L_, idx_) };
    UserValueIndex _nuv{ 0 };
    do {
        // we don't know how many uservalues we are going to extract, there might be a lot...
        STACK_GROW(L_, 1);
    } while (lua_getiuservalue(L_, _idx, ++_nuv) != LUA_TNONE);                                    // L_: ... [uv]* nil
    // last call returned TNONE and pushed nil, that we don't need
    lua_pop(L_, 1);                                                                                // L_: ... [uv]*
    --_nuv;
    STACK_CHECK(L_, _nuv);
    return UserValueCount{ _nuv.value() };
}

// #################################################################################################

// a small helper to obtain a module's table from the registry instead of relying on the presence of _G["<name>"]
LuaType luaW_getmodule(lua_State* const L_, std::string_view const& name_)
{
    STACK_CHECK_START_REL(L_, 0);
    LuaType _type{ luaW_getfield(L_, kIdxRegistry, LUA_LOADED_TABLE) };                            // L_: _R._LOADED|nil
    if (_type != LuaType::TABLE) {                                                                 // L_: _R._LOADED|nil
        STACK_CHECK(L_, 1);
        return _type;
    }
    _type = luaW_getfield(L_, kIdxTop, name_);                                                     // L_: _R._LOADED {module}|nil
    lua_remove(L_, -2);                                                                            // L_: {module}|nil
    STACK_CHECK(L_, 1);
    return _type;
}

// #################################################################################################
// #################################################################################################
#if LUA_VERSION_NUM == 501
// #################################################################################################
// #################################################################################################

// Copied from Lua 5.2 loadlib.c
int luaL_getsubtable(lua_State* const L_, StackIndex const idx_, char const* fname_)
{
    lua_getfield(L_, idx_, fname_);
    if (lua_istable(L_, -1)) {
        return 1; /* table already there */
    } else {
        lua_pop(L_, 1); /* remove previous result */
        StackIndex const _absidx{ luaW_absindex(L_, idx_) };
        lua_newtable(L_);
        lua_pushvalue(L_, -1); /* copy to be left at top */
        lua_setfield(L_, _absidx, fname_); /* assign new table to field */
        return 0; /* false, because did not find table there */
    }
}
// #################################################################################################

void luaL_requiref(lua_State* L_, const char* modname_, lua_CFunction openf_, int glb_)
{
    lua_pushcfunction(L_, openf_);
    lua_pushstring(L_, modname_); /* argument to open function */
    lua_call(L_, 1, 1); /* open module */
    luaL_getsubtable(L_, kIdxRegistry, LUA_LOADED_TABLE);
    lua_pushvalue(L_, -2); /* make copy of module (call result) */
    lua_setfield(L_, -2, modname_); /* _LOADED[modname] = module */
    lua_pop(L_, 1); /* remove _LOADED table */
    if (glb_) {
        lua_pushvalue(L_, -1); /* copy of 'mod' */
        lua_setglobal(L_, modname_); /* _G[modname] = module */
    }
}
#endif // LUA_VERSION_NUM

// #################################################################################################
// #################################################################################################
#if LUA_VERSION_NUM < 504
// #################################################################################################
// #################################################################################################

void* lua_newuserdatauv(lua_State* const L_, size_t const sz_, [[maybe_unused]] UserValueCount const nuvalue_)
{
    LUA_ASSERT(L_, nuvalue_ <= 1);
    return lua_newuserdata(L_, sz_);
}

// #################################################################################################

// push on stack uservalue #n of full userdata at idx
int lua_getiuservalue(lua_State* const L_, StackIndex const idx_, UserValueIndex const n_)
{
    STACK_CHECK_START_REL(L_, 0);
    // full userdata can have only 1 uservalue before 5.4
    if (n_ > 1) {
        lua_pushnil(L_);
        return LUA_TNONE;
    }

#if LUA_VERSION_NUM == 501
    lua_getfenv(L_, idx_);                                                                         // L_: ... {}|nil
    STACK_CHECK(L_, 1);
    // default environment is not a nil (see lua_getfenv)
    lua_getglobal(L_, LUA_LOADLIBNAME);                                                            // L_: ... {}|nil package
    if (lua_rawequal(L_, -2, -1) || lua_rawequal(L_, -2, LUA_GLOBALSINDEX)) {
        lua_pop(L_, 2);                                                                            // L_: ...
        lua_pushnil(L_);                                                                           // L_: ... nil
        STACK_CHECK(L_, 1);
        return LUA_TNONE;
    }
    else {
        lua_pop(L_, 1);                                                                            // L_: ... nil
    }
#else // LUA_VERSION_NUM > 501
    lua_getuservalue(L_, idx_);                                                                    // L_: {}|nil
#endif// LUA_VERSION_NUM > 501
    STACK_CHECK(L_, 1);
    int const _uvType{ lua_type(L_, -1) };
    // under Lua 5.2 and 5.3, there is a single uservalue, that can be nil.
    // emulate 5.4 behavior by returning LUA_TNONE when that's the case
    return (_uvType == LUA_TNIL) ? LUA_TNONE : _uvType;
}

// #################################################################################################

// Pops a value from the stack and sets it as the new n-th user value associated to the full userdata at the given index.
// Returns 0 if the userdata does not have that value.
int lua_setiuservalue(lua_State* const L_, StackIndex const idx_, UserValueIndex const n_)
{
    if (n_ > 1
#if LUA_VERSION_NUM == 501
        || lua_type(L_, -1) != LUA_TTABLE
#endif
    ) {
        lua_pop(L_, 1);
        return 0;
    }

#if LUA_VERSION_NUM == 501
    lua_setfenv(L_, idx_);
#else // LUA_VERSION_NUM == 501
    lua_setuservalue(L_, idx_);
#endif // LUA_VERSION_NUM == 501
    return 1; // I guess anything non-0 is ok
}

#endif // LUA_VERSION_NUM

// #################################################################################################

#if LUA_VERSION_NUM < 505

#include <time.h>

/* Size for the buffer, in bytes */
#define BUFSEEDB (sizeof(void*) + sizeof(time_t))

/* Size for the buffer in int's, rounded up */
#define BUFSEED ((BUFSEEDB + sizeof(int) - 1) / sizeof(int))

/*
** Copy the contents of variable 'v' into the buffer pointed by 'b'.
** (The '&b[0]' disguises 'b' to fix an absurd warning from clang.)
*/
#define addbuff(b, v) (memcpy(&b[0], &(v), sizeof(v)), b += sizeof(v))

// Copied from Lua 5.5 lauxlib.c
unsigned int luaL_makeseed(lua_State*)
{
    unsigned int buff[BUFSEED];
    unsigned int res;
    unsigned int i;
    time_t t = time(nullptr);
    char* b = (char*) buff;
    addbuff(b, b); /* local variable's address */
    addbuff(b, t); /* time */
    /* fill (rare but possible) remain of the buffer with zeros */
    memset(b, 0, sizeof(buff) - BUFSEEDB);
    res = buff[0];
    for (i = 1; i < BUFSEED; i++)
        res ^= (res >> 3) + (res << 7) + buff[i];
    return res;
}

#endif // LUA_VERSION_NUM < 505
