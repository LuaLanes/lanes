/*
 * ###############################################################################################
 * ######################################### Lua 5.1/5.2 #########################################
 * ###############################################################################################
 */
#include "compat.h"
#include "macros_and_utils.h"

/*
** Copied from Lua 5.2 loadlib.c
*/
#if LUA_VERSION_NUM == 501
static int luaL_getsubtable (lua_State *L, int idx, const char *fname)
{
    lua_getfield(L, idx, fname);
    if (lua_istable(L, -1))
        return 1;  /* table already there */
    else
    {
        lua_pop(L, 1);  /* remove previous result */
        idx = lua_absindex(L, idx);
        lua_newtable(L);
        lua_pushvalue(L, -1);  /* copy to be left at top */
        lua_setfield(L, idx, fname);  /* assign new table to field */
        return 0;  /* false, because did not find table there */
    }
}

void luaL_requiref (lua_State *L, const char *modname, lua_CFunction openf, int glb)
{
    lua_pushcfunction(L, openf);
    lua_pushstring(L, modname);  /* argument to open function */
    lua_call(L, 1, 1);  /* open module */
    luaL_getsubtable(L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
    lua_pushvalue(L, -2);  /* make copy of module (call result) */
    lua_setfield(L, -2, modname);  /* _LOADED[modname] = module */
    lua_pop(L, 1);  /* remove _LOADED table */
    if (glb)
    {
        lua_pushvalue(L, -1);  /* copy of 'mod' */
        lua_setglobal(L, modname);  /* _G[modname] = module */
    }
}
#endif // LUA_VERSION_NUM

#if LUA_VERSION_NUM < 504

void* lua_newuserdatauv( lua_State* L, size_t sz, int nuvalue)
{
    ASSERT_L( nuvalue <= 1);
    return lua_newuserdata( L, sz);
}

int lua_getiuservalue( lua_State* L, int idx, int n)
{
    if( n > 1)
    {
        lua_pushnil( L);
        return LUA_TNONE;
    }
    lua_getuservalue( L, idx);

#if LUA_VERSION_NUM == 501
    /* default environment is not a nil (see lua_getfenv) */
    lua_getglobal(L, "package");
    if (lua_rawequal(L, -2, -1) || lua_rawequal(L, -2, LUA_GLOBALSINDEX))
    {
        lua_pop(L, 2);
        lua_pushnil( L);

        return LUA_TNONE;
    }
    lua_pop(L, 1);	/* remove package */
#endif

    return lua_type( L, -1);
}

int lua_setiuservalue( lua_State* L, int idx, int n)
{
    if( n > 1
#if LUA_VERSION_NUM == 501
        || lua_type( L, -1) != LUA_TTABLE
#endif
        )
    {
        lua_pop( L, 1);
        return 0;
    }

    (void) lua_setuservalue( L, idx);
    return 1; // I guess anything non-0 is ok
}

#endif // LUA_VERSION_NUM

