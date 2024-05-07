// #################################################################################################
// ###################################### Lua 5.1 / 5.2 / 5.3 ######################################
// #################################################################################################

#include "compat.h"
#include "macros_and_utils.h"

// #################################################################################################

// a small helper to obtain a module's table from the registry instead of relying on the presence of _G["<name>"]
LuaType luaG_getmodule(lua_State* L_, char const* name_)
{
    STACK_CHECK_START_REL(L_, 0);
    LuaType type{ static_cast<LuaType>(lua503_getfield(L_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE)) };// L_: _R._LOADED|nil
    if (type != LuaType::TABLE) {                                                                  // L_: _R._LOADED|nil
        STACK_CHECK(L_, 1);
        return type;
    }
    type = static_cast<LuaType>(lua503_getfield(L_, -1, name_));                                   // L_: _R._LOADED {module}|nil
    lua_remove(L_, -2);                                                                            // L_: {module}|nil
    STACK_CHECK(L_, 1);
    return type;
}

// #################################################################################################
// #################################################################################################
#if LUA_VERSION_NUM == 501
// #################################################################################################
// #################################################################################################

// Copied from Lua 5.2 loadlib.c
int luaL_getsubtable(lua_State* L_, int idx_, const char* fname_)
{
    lua_getfield(L_, idx_, fname_);
    if (lua_istable(L_, -1))
        return 1; /* table already there */
    else {
        lua_pop(L_, 1); /* remove previous result */
        idx_ = lua_absindex(L_, idx_);
        lua_newtable(L_);
        lua_pushvalue(L_, -1); /* copy to be left at top */
        lua_setfield(L_, idx_, fname_); /* assign new table to field */
        return 0; /* false, because did not find table there */
    }
}

// #################################################################################################

void luaL_requiref(lua_State* L_, const char* modname_, lua_CFunction openf_, int glb_)
{
    lua_pushcfunction(L_, openf_);
    lua_pushstring(L_, modname_); /* argument to open function */
    lua_call(L_, 1, 1); /* open module */
    luaL_getsubtable(L_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);
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

void* lua_newuserdatauv(lua_State* L_, size_t sz_, int nuvalue_)
{
    LUA_ASSERT(L_, nuvalue_ <= 1);
    return lua_newuserdata(L_, sz_);
}

// #################################################################################################

// push on stack uservalue #n of full userdata at idx
int lua_getiuservalue(lua_State* L_, int idx_, int n_)
{
    // full userdata can have only 1 uservalue before 5.4
    if (n_ > 1) {
        lua_pushnil(L_);
        return LUA_TNONE;
    }
    lua_getuservalue(L_, idx_);

#if LUA_VERSION_NUM == 501
    /* default environment is not a nil (see lua_getfenv) */
    lua_getglobal(L_, LUA_LOADLIBNAME);
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
int lua_setiuservalue(lua_State* L_, int idx_, int n_)
{
    if (n_ > 1
#if LUA_VERSION_NUM == 501
        || lua_type(L_, -1) != LUA_TTABLE
#endif
    ) {
        lua_pop(L_, 1);
        return 0;
    }

    lua_setuservalue(L_, idx_);
    return 1; // I guess anything non-0 is ok
}

#endif // LUA_VERSION_NUM
