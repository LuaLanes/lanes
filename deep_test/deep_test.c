#include <malloc.h>
#include <memory.h>
#include <assert.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "lanes/src/deep.h"
#include "lanes/src/compat.h"

#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#define LANES_API __declspec(dllexport)
#else
#define LANES_API
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

// ################################################################################################

// a lanes-deep userdata. needs DeepPrelude and luaG_newdeepuserdata from Lanes code.
struct s_MyDeepUserdata
{
	DeepPrelude prelude; // Deep userdata MUST start with this header
	lua_Integer val;
};
static void* deep_test_id( lua_State* L, enum eDeepOp op_);

// ################################################################################################

static int deep_set( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	lua_Integer i = lua_tointeger( L, 2);
	self->val = i;
	return 0;
}

// ################################################################################################

// won't actually do anything as deep userdata don't have uservalue slots
static int deep_setuv( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	int uv = (int) luaL_optinteger( L, 2, 1);
	lua_settop( L, 3);
	lua_pushboolean( L, lua_setiuservalue( L, 1, uv) != 0);
	return 1;
}

// ################################################################################################

// won't actually do anything as deep userdata don't have uservalue slots
static int deep_getuv( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	int uv = (int) luaL_optinteger( L, 2, 1);
	lua_getiuservalue( L, 1, uv);
	return 1;
}

// ################################################################################################

static int deep_tostring( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	lua_pushfstring( L, "%p:deep(%d)", lua_topointer( L, 1), self->val);
	return 1;
}

// ################################################################################################

static int deep_gc( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	return 0;
}

// ################################################################################################

static luaL_Reg const deep_mt[] =
{
	{ "__tostring", deep_tostring},
	{ "__gc", deep_gc},
	{ "set", deep_set},
	{ "setuv", deep_setuv},
	{ "getuv", deep_getuv},
	{ NULL, NULL }
};

// ################################################################################################

static void* deep_test_id( lua_State* L, enum eDeepOp op_)
{
	switch( op_)
	{
		case eDO_new:
		{
			struct s_MyDeepUserdata* deep_test = (struct s_MyDeepUserdata*) malloc( sizeof(struct s_MyDeepUserdata));
			deep_test->prelude.magic.value = DEEP_VERSION.value;
			deep_test->val = 0;
			return deep_test;
		}

		case eDO_delete:
		{
			struct s_MyDeepUserdata* deep_test = (struct s_MyDeepUserdata*) lua_touserdata( L, 1);
			free( deep_test);
			return NULL;
		}

		case eDO_metatable:
		{
			luaL_getmetatable( L, "deep");             // mt
			return NULL;
		}

		case eDO_module:
		return "deep_test";

		default:
		{
			return NULL;
		}
	}
}

// ################################################################################################

int luaD_new_deep( lua_State* L)
{
	int nuv = (int) luaL_optinteger( L, 1, 0);
	// no additional parameter to luaG_newdeepuserdata!
	lua_settop( L, 0);
	return luaG_newdeepuserdata( L, deep_test_id, nuv);
}

// ################################################################################################
// ################################################################################################

struct s_MyClonableUserdata
{
	lua_Integer val;
};

// ################################################################################################

static int clonable_set( lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	lua_Integer i = lua_tointeger( L, 2);
	self->val = i;
	return 0;
}

// ################################################################################################

static int clonable_setuv( lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	int uv = (int) luaL_optinteger( L, 2, 1);
	lua_settop( L, 3);
	lua_pushboolean( L, lua_setiuservalue( L, 1, uv) != 0);
	return 1;
}

// ################################################################################################

static int clonable_getuv( lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	int uv = (int) luaL_optinteger( L, 2, 1);
	lua_getiuservalue( L, 1, uv);
	return 1;
}

// ################################################################################################

static int clonable_tostring(lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	lua_pushfstring( L, "%p:clonable(%d)", lua_topointer( L, 1), self->val);
	return 1;
}

// ################################################################################################

static int clonable_gc( lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	return 0;
}

// ################################################################################################

// this is all we need to make a userdata lanes-clonable. no dependency on Lanes code.
static int clonable_lanesclone( lua_State* L)
{
	switch( lua_gettop( L))
	{
		case 3:
		{
			struct s_MyClonableUserdata* self = lua_touserdata( L, 1);
			struct s_MyClonableUserdata* from = lua_touserdata( L, 2);
			size_t len = lua_tointeger( L, 3);
			assert( len == sizeof(struct s_MyClonableUserdata));
			*self = *from;
		}
		return 0;

		default:
		(void) luaL_error( L, "Lanes called clonable_lanesclone with unexpected parameters");
	}
	return 0;
}

// ################################################################################################

static luaL_Reg const clonable_mt[] =
{
	{ "__tostring", clonable_tostring},
	{ "__gc", clonable_gc},
	{ "__lanesclone", clonable_lanesclone},
	{ "set", clonable_set},
	{ "setuv", clonable_setuv},
	{ "getuv", clonable_getuv},
	{ NULL, NULL }
};

// ################################################################################################

int luaD_new_clonable( lua_State* L)
{
	int nuv = (int) luaL_optinteger( L, 1, 1);
	lua_newuserdatauv( L, sizeof( struct s_MyClonableUserdata), nuv);
	luaL_setmetatable( L, "clonable");
	return 1;
}

// ################################################################################################
// ################################################################################################

static luaL_Reg const deep_module[] =
{
	{ "new_deep", luaD_new_deep},
	{ "new_clonable", luaD_new_clonable},
	{ NULL, NULL}
};

// ################################################################################################

extern int __declspec(dllexport) luaopen_deep_test(lua_State* L)
{
	luaL_newlib( L, deep_module);                           // M

	// preregister the metatables for the types we can instantiate so that Lanes can know about them
	if( luaL_newmetatable( L, "clonable"))                  // M mt
	{
		luaL_setfuncs( L, clonable_mt, 0);
		lua_pushvalue(L, -1);                                 // M mt mt
		lua_setfield(L, -2, "__index");                       // M mt
	}
	lua_setfield(L, -2, "__clonableMT");                    // M

	if( luaL_newmetatable( L, "deep"))                      // mt
	{
		luaL_setfuncs( L, deep_mt, 0);
		lua_pushvalue(L, -1);                                 // mt mt
		lua_setfield(L, -2, "__index");                       // mt
	}
	lua_setfield(L, -2, "__deepMT");                    // M

	return 1;
}
