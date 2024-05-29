#include "lanes/src/deep.h"
#include "lanes/src/compat.h"

#include <malloc.h>
#include <memory.h>
#include <assert.h>

class MyDeepFactory : public DeepFactory
{
    public:

    static MyDeepFactory Instance;

    private:

    void createMetatable(lua_State* const L_) const override
    {
        luaL_getmetatable(L_, "deep");
    }
    void deleteDeepObjectInternal(lua_State* const L_, DeepPrelude* o_) const override;
    [[nodiscard]] DeepPrelude* newDeepObjectInternal(lua_State* const L_) const override;
    [[nodiscard]] std::string_view moduleName() const override { return std::string_view{ "deep_test" }; }
};
/*static*/ MyDeepFactory MyDeepFactory::Instance{};

// #################################################################################################

// a lanes-deep userdata. needs DeepPrelude and luaG_newdeepuserdata from Lanes code.
struct MyDeepUserdata : public DeepPrelude // Deep userdata MUST start with a DeepPrelude
{
    lua_Integer val{ 0 };
};

// #################################################################################################

DeepPrelude* MyDeepFactory::newDeepObjectInternal(lua_State* const L_) const
{
    MyDeepUserdata* const _deep_test{ new MyDeepUserdata{ MyDeepFactory::Instance } };
    return _deep_test;
}

// #################################################################################################

void MyDeepFactory::deleteDeepObjectInternal(lua_State* const L_, DeepPrelude* const o_) const
{
    MyDeepUserdata* const _deep_test{ static_cast<MyDeepUserdata*>(o_) };
    delete _deep_test;
}

// #################################################################################################

[[nodiscard]] static int deep_set(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, 1)) };
    lua_Integer i = lua_tointeger(L_, 2);
    _self->val = i;
    return 0;
}

// #################################################################################################

[[nodiscard]] static int deep_setuv(lua_State* L)
{
    MyDeepUserdata* const self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, 1)) };
    int uv = (int) luaL_optinteger(L, 2, 1);
    lua_settop( L, 3);
    lua_pushboolean( L, lua_setiuservalue( L, 1, uv) != 0);
    return 1;
}

// #################################################################################################

// won't actually do anything as deep userdata don't have uservalue slots
[[nodiscard]] static int deep_getuv(lua_State* L)
{
    MyDeepUserdata* const self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, 1)) };
    int uv = (int) luaL_optinteger(L, 2, 1);
    lua_getiuservalue( L, 1, uv);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int deep_tostring(lua_State* L)
{
    MyDeepUserdata* const self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, 1)) };
    lua_pushfstring(L, "%p:deep(%d)", lua_topointer(L, 1), self->val);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int deep_gc(lua_State* L)
{
    MyDeepUserdata* const self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, 1)) };
    return 0;
}

// #################################################################################################

static luaL_Reg const deep_mt[] =
{
    { "__tostring", deep_tostring},
    { "__gc", deep_gc},
    { "set", deep_set},
    { "setuv", deep_setuv},
    { "getuv", deep_getuv},
    { nullptr, nullptr }
};

// #################################################################################################

int luaD_new_deep( lua_State* L)
{
    int const nuv{ static_cast<int>(luaL_optinteger(L, 1, 0)) };
    lua_settop(L, 0);
    return MyDeepFactory::Instance.pushDeepUserdata(DestState{ L }, nuv);
}

// #################################################################################################
// #################################################################################################

struct MyClonableUserdata
{
    lua_Integer val;
};

// #################################################################################################

[[nodiscard]] static int clonable_set(lua_State* L)
{
    MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
    lua_Integer i = lua_tointeger(L, 2);
    self->val = i;
    return 0;
}

// #################################################################################################

[[nodiscard]] static int clonable_setuv(lua_State* L)
{
    MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
    int uv = (int) luaL_optinteger(L, 2, 1);
    lua_settop( L, 3);
    lua_pushboolean( L, lua_setiuservalue( L, 1, uv) != 0);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int clonable_getuv(lua_State* L)
{
    MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
    int uv = (int) luaL_optinteger(L, 2, 1);
    lua_getiuservalue( L, 1, uv);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int clonable_tostring(lua_State* L)
{
    MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
    lua_pushfstring(L, "%p:clonable(%d)", lua_topointer(L, 1), self->val);
    return 1;
}

// #################################################################################################

[[nodiscard]] static int clonable_gc(lua_State* L)
{
    MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
    return 0;
}

// #################################################################################################

// this is all we need to make a userdata lanes-clonable. no dependency on Lanes code.
[[nodiscard]] static int clonable_lanesclone(lua_State* L)
{
    switch( lua_gettop(L))
    {
        case 3:
        {
            MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
            MyClonableUserdata* from = static_cast<MyClonableUserdata*>(lua_touserdata(L, 2));
            size_t len = lua_tointeger(L, 3);
            assert( len == sizeof(MyClonableUserdata));
            *self = *from;
        }
        return 0;

        default:
        raise_luaL_error(L, "Lanes called clonable_lanesclone with unexpected parameters");
    }
    return 0;
}

// #################################################################################################

static luaL_Reg const clonable_mt[] =
{
    { "__tostring", clonable_tostring},
    { "__gc", clonable_gc},
    { "__lanesclone", clonable_lanesclone},
    { "set", clonable_set},
    { "setuv", clonable_setuv},
    { "getuv", clonable_getuv},
    { nullptr, nullptr }
};

// #################################################################################################

int luaD_new_clonable( lua_State* L)
{
    int const nuv{ static_cast<int>(luaL_optinteger(L, 1, 1)) };
    lua_newuserdatauv( L, sizeof(MyClonableUserdata), nuv);
    luaL_setmetatable( L, "clonable");
    return 1;
}

// #################################################################################################
// #################################################################################################

static luaL_Reg const deep_module[] =
{
    { "new_deep", luaD_new_deep},
    { "new_clonable", luaD_new_clonable},
    { nullptr, nullptr }
};

// #################################################################################################

LANES_API int luaopen_deep_test(lua_State* L)
{
    luaL_newlib( L, deep_module);                           // M

    // preregister the metatables for the types we can instantiate so that Lanes can know about them
    if (luaL_newmetatable( L, "clonable"))                  // M mt
    {
        luaL_setfuncs( L, clonable_mt, 0);
        lua_pushvalue(L, -1);                               // M mt mt
        lua_setfield(L, -2, "__index");                     // M mt
    }
    lua_setfield(L, -2, "__clonableMT");                    // M

    if (luaL_newmetatable( L, "deep"))                      // mt
    {
        luaL_setfuncs( L, deep_mt, 0);
        lua_pushvalue(L, -1);                               // mt mt
        lua_setfield(L, -2, "__index");                     // mt
    }
    lua_setfield(L, -2, "__deepMT");                        // M

    return 1;
}
