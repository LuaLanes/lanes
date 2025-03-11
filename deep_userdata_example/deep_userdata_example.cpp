#include "lanes/src/_pch.hpp"
#include "lanes/src/deep.hpp"
#include "lanes/src/compat.hpp"

class MyDeepFactory final : public DeepFactory
{
    public:
    static MyDeepFactory Instance;

    private:
    ~MyDeepFactory() override = default;

    private:
    void createMetatable(lua_State* const L_) const override
    {
        luaL_getmetatable(L_, "deep");
    }
    void deleteDeepObjectInternal(lua_State* const L_, DeepPrelude* o_) const override;
    [[nodiscard]]
    DeepPrelude* newDeepObjectInternal(lua_State* const L_) const override;
    [[nodiscard]]
    std::string_view moduleName() const override { return std::string_view{ "deep_userdata_example" }; }
};
/*static*/ MyDeepFactory MyDeepFactory::Instance{};

// #################################################################################################

// a lanes-deep userdata. needs DeepPrelude and luaG_newdeepuserdata from Lanes code.
struct MyDeepUserdata : public DeepPrelude // Deep userdata MUST start with a DeepPrelude
{
    std::atomic<int> inUse{};
    lua_Integer val{ 0 };
    MyDeepUserdata()
    : DeepPrelude{ MyDeepFactory::Instance }
    {
    }
};

// #################################################################################################

DeepPrelude* MyDeepFactory::newDeepObjectInternal(lua_State* const L_) const
{
    MyDeepUserdata* const _deep_test{ new MyDeepUserdata };
    return _deep_test;
}

// #################################################################################################

void MyDeepFactory::deleteDeepObjectInternal(lua_State* const L_, DeepPrelude* const o_) const
{
    MyDeepUserdata* const _deep_test{ static_cast<MyDeepUserdata*>(o_) };
    delete _deep_test;
}

// #################################################################################################

[[nodiscard]]
static int deep_gc(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, StackIndex{ 1 })) };
    luaL_argcheck(L_, 1, !_self->inUse.load(), "being collected while in use!");
    if (lua_getiuservalue(L_, kIdxTop, UserValueIndex{ 1 }) == LUA_TFUNCTION) {
        lua_call(L_, 0, 0);
    }
    return 0;
}

// #################################################################################################

[[nodiscard]]
static int deep_get(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, StackIndex{ 1 })) };
    lua_pushinteger(L_, _self->val);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int deep_tostring(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, StackIndex{ 1 })) };
    _self->inUse.fetch_add(1, std::memory_order_seq_cst);
    luaG_pushstring(L_, "%p:deep(%d)", _self, _self->val);
    _self->inUse.fetch_sub(1, std::memory_order_seq_cst);
    return 1;
}

// #################################################################################################

// won't actually do anything as deep userdata don't have uservalue slots
[[nodiscard]]
static int deep_getuv(lua_State* L)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, StackIndex{ 1 })) };
    _self->inUse.fetch_add(1, std::memory_order_seq_cst);
    UserValueIndex const _uv{ static_cast<UserValueIndex::type>(luaL_optinteger(L, 2, 1)) };
    lua_getiuservalue(L, StackIndex{ 1 }, _uv);
    _self->inUse.fetch_sub(1, std::memory_order_seq_cst);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int deep_invoke(lua_State* L)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, StackIndex{ 1 })) };
    luaL_argcheck(L, 2, lua_gettop(L) >= 2, "need something to call");
    _self->inUse.fetch_add(1, std::memory_order_seq_cst);
    lua_call(L, lua_gettop(L) - 2, LUA_MULTRET);
    _self->inUse.fetch_sub(1, std::memory_order_seq_cst);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int deep_refcount(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, StackIndex{ 1 })) };
    lua_pushinteger(L_, _self->getRefcount());
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int deep_set(lua_State* const L_)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L_, StackIndex{ 1 })) };
    _self->inUse.fetch_add(1, std::memory_order_seq_cst);
    lua_Integer _i = lua_tointeger(L_, 2);
    _self->val = _i;
    _self->inUse.fetch_sub(1, std::memory_order_seq_cst);
    return 0;
}

// #################################################################################################

[[nodiscard]]
static int deep_setuv(lua_State* L)
{
    MyDeepUserdata* const _self{ static_cast<MyDeepUserdata*>(MyDeepFactory::Instance.toDeep(L, StackIndex{ 1 })) };
    _self->inUse.fetch_add(1, std::memory_order_seq_cst);
    UserValueIndex const _uv{ static_cast<UserValueIndex::type>(luaL_optinteger(L, 2, 1)) };
    lua_settop(L, 3);
    lua_pushboolean(L, lua_setiuservalue(L, StackIndex{ 1 }, _uv) != 0);
    _self->inUse.fetch_sub(1, std::memory_order_seq_cst);
    return 1;
}

// #################################################################################################

static luaL_Reg const deep_mt[] = {
    { "__gc", deep_gc },
    { "__tostring", deep_tostring },
    { "get", deep_get },
    { "getuv", deep_getuv },
    { "invoke", deep_invoke },
    { "refcount", deep_refcount },
    { "set", deep_set },
    { "setuv", deep_setuv },
    { nullptr, nullptr }
};

// #################################################################################################

int luaD_get_deep_count(lua_State* const L_)
{
    lua_pushinteger(L_, MyDeepFactory::Instance.getObjectCount());
    return 1;
}

// #################################################################################################

int luaD_new_deep(lua_State* L)
{
    UserValueCount const _nuv{ static_cast<int>(luaL_optinteger(L, 1, 0)) };
    lua_settop(L, 0);
    MyDeepFactory::Instance.pushDeepUserdata(DestState{ L }, _nuv);
    return 1;
}

// #################################################################################################
// #################################################################################################

struct MyClonableUserdata
{
    lua_Integer val;
};

// #################################################################################################

[[nodiscard]]
static int clonable_get(lua_State* const L_)
{
    MyClonableUserdata* const _self{ static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1)) };
    lua_pushinteger(L_, _self->val);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int clonable_set(lua_State* const L_)
{
    MyClonableUserdata* _self = static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1));
    lua_Integer i = lua_tointeger(L_, 2);
    _self->val = i;
    return 0;
}

// #################################################################################################

[[nodiscard]]
static int clonable_setuv(lua_State* const L_)
{
    [[maybe_unused]] MyClonableUserdata* const _self{ static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1)) };
    UserValueIndex const _uv{ static_cast<UserValueIndex::type>(luaL_optinteger(L_, 2, 1)) };
    lua_settop(L_, 3);
    lua_pushboolean(L_, lua_setiuservalue(L_, StackIndex{ 1 }, _uv) != 0);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int clonable_getuv(lua_State* const L_)
{
    [[maybe_unused]] MyClonableUserdata* const _self{ static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1)) };
    UserValueIndex const _uv{ static_cast<UserValueIndex::type>(luaL_optinteger(L_, 2, 1)) };
    lua_getiuservalue(L_, StackIndex{ 1 }, _uv);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int clonable_tostring(lua_State* const L_)
{
    MyClonableUserdata* _self = static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1));
    luaG_pushstring(L_, "%p:clonable(%d)", lua_topointer(L_, 1), _self->val);
    return 1;
}

// #################################################################################################

[[nodiscard]]
static int clonable_gc(lua_State* const L_)
{
    [[maybe_unused]] MyClonableUserdata* _self = static_cast<MyClonableUserdata*>(lua_touserdata(L_, 1));
    if (lua_getiuservalue(L_, kIdxTop, UserValueIndex{ 1 }) == LUA_TFUNCTION) {
        lua_call(L_, 0, 0);
    }
    return 0;
}

// #################################################################################################

// this is all we need to make a userdata lanes-clonable. no dependency on Lanes code.
[[nodiscard]]
static int clonable_lanesclone(lua_State* L)
{
    switch (lua_gettop(L)) {
    case 3:
        {
            MyClonableUserdata* self = static_cast<MyClonableUserdata*>(lua_touserdata(L, 1));
            MyClonableUserdata* from = static_cast<MyClonableUserdata*>(lua_touserdata(L, 2));
            size_t len = lua_tointeger(L, 3);
            assert(len == sizeof(MyClonableUserdata));
            *self = *from;
        }
        return 0;

    default:
        raise_luaL_error(L, "Lanes called clonable_lanesclone with unexpected arguments");
    }
    return 0;
}

// #################################################################################################

static luaL_Reg const clonable_mt[] = {
    { "__gc", clonable_gc },
    { "__lanesclone", clonable_lanesclone },
    { "__tostring", clonable_tostring },
    { "get", clonable_get }, 
    { "set", clonable_set },
    { "setuv", clonable_setuv },
    { "getuv", clonable_getuv },
    { nullptr, nullptr }
};

// #################################################################################################

int luaD_new_clonable(lua_State* L)
{
    UserValueCount const _nuv{ static_cast<int>(luaL_optinteger(L, 1, 1)) };
    lua_newuserdatauv(L, sizeof(MyClonableUserdata), _nuv);
    luaG_setmetatable(L, "clonable");
    return 1;
}

// #################################################################################################
// #################################################################################################

static luaL_Reg const deep_module[] = {
    { "get_deep_count", luaD_get_deep_count }, 
    { "new_deep", luaD_new_deep },
    { "new_clonable", luaD_new_clonable },
    { nullptr, nullptr }
};

// #################################################################################################

LANES_API int luaopen_deep_userdata_example(lua_State* L)
{
    luaG_newlib<std::size(deep_module)>(L, deep_module);                                           // M

    // preregister the metatables for the types we can instantiate so that Lanes can know about them
    if (luaL_newmetatable(L, "clonable"))                                                          // M mt
    {
        luaG_registerlibfuncs(L, clonable_mt);
        lua_pushvalue(L, -1);                                                                      // M mt mt
        lua_setfield(L, -2, "__index");                                                            // M mt
    }
    lua_setfield(L, -2, "__clonableMT");                                                           // M

    if (luaL_newmetatable(L, "deep"))                                                              // mt
    {
        luaG_registerlibfuncs(L, deep_mt);
        lua_pushvalue(L, -1);                                                                      // mt mt
        lua_setfield(L, -2, "__index");                                                            // mt
    }
    lua_setfield(L, -2, "__deepMT");                                                               // M

    return 1;
}
