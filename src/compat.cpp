// #################################################################################################
// ###################################### Lua 5.1 / 5.2 / 5.3 ######################################
// #################################################################################################

#include "compat.h"
#include "macros_and_utils.h"

// #################################################################################################
// #################################################################################################
#if LUA_VERSION_NUM == 501
// #################################################################################################
// #################################################################################################

// Copied from Lua 5.2 loadlib.c
static int luaL_getsubtable(lua_State* L_, int idx, const char* fname)
{
    lua_getfield(L_, idx, fname);
    if (lua_istable(L_, -1))
        return 1; /* table already there */
    else {
        lua_pop(L_, 1); /* remove previous result */
        idx = lua_absindex(L_, idx);
        lua_newtable(L_);
        lua_pushvalue(L_, -1); /* copy to be left at top */
        lua_setfield(L_, idx, fname); /* assign new table to field */
        return 0; /* false, because did not find table there */
    }
}

// #################################################################################################

void luaL_requiref(lua_State* L_, const char* modname, lua_CFunction openf, int glb)
{
    lua_pushcfunction(L_, openf);
    lua_pushstring(L_, modname); /* argument to open function */
    lua_call(L_, 1, 1); /* open module */
    luaL_getsubtable(L_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_pushvalue(L_, -2); /* make copy of module (call result) */
    lua_setfield(L_, -2, modname); /* _LOADED[modname] = module */
    lua_pop(L_, 1); /* remove _LOADED table */
    if (glb) {
        lua_pushvalue(L_, -1); /* copy of 'mod' */
        lua_setglobal(L_, modname); /* _G[modname] = module */
    }
}
#endif // LUA_VERSION_NUM

// #################################################################################################
// #################################################################################################
#if LUA_VERSION_NUM < 504
// #################################################################################################
// #################################################################################################

void* lua_newuserdatauv(lua_State* L_, size_t sz, int nuvalue)
{
    LUA_ASSERT(L_, nuvalue <= 1);
    return lua_newuserdata(L_, sz);
}

// #################################################################################################

// push on stack uservalue #n of full userdata at idx
int lua_getiuservalue(lua_State* L_, int idx, int n)
{
    // full userdata can have only 1 uservalue before 5.4
    if (n > 1) {
        lua_pushnil(L_);
        return LUA_TNONE;
    }
    lua_getuservalue(L_, idx);

#if LUA_VERSION_NUM == 501
    /* default environment is not a nil (see lua_getfenv) */
    lua_getglobal(L_, "package");
    if (lua_rawequal(L_, -2, -1) || lua_rawequal(L_, -2, LUA_GLOBALSINDEX)) {
        lua_pop(L_, 2);
        lua_pushnil(L_);

        return LUA_TNONE;
    }
    lua_pop(L_, 1); /* remove package */
#endif

    return lua_type(L_, -1);
}

// #################################################################################################

// Pops a value from the stack and sets it as the new n-th user value associated to the full userdata at the given index.
// Returns 0 if the userdata does not have that value.
int lua_setiuservalue(lua_State* L_, int idx, int n)
{
    if (n > 1
#if LUA_VERSION_NUM == 501
        || lua_type(L_, -1) != LUA_TTABLE
#endif
    ) {
        lua_pop(L_, 1);
        return 0;
    }

    lua_setuservalue(L_, idx);
    return 1; // I guess anything non-0 is ok
}

#endif // LUA_VERSION_NUM
