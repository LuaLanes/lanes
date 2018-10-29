#include <malloc.h>
#include <memory.h>

#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "deep.h"

#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#define LANES_API __declspec(dllexport)
#else
#define LANES_API
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

// ################################################################################################

struct s_MyDeepUserdata
{
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

static int deep_tostring( lua_State* L)
{
	struct s_MyDeepUserdata* self = luaG_todeep( L, deep_test_id, 1);
	lua_pushfstring( L, "deep(%d)", self->val);
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
			lua_newtable( L);                          // mt
			luaL_setfuncs( L, deep_mt, 0);             // mt
			lua_pushvalue( L, -1);                     // mt mt
			lua_setfield( L, -2, "__index");           // mt
			luaG_pushdeepversion( L);                  // mt version
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
	return luaG_newdeepuserdata( L, deep_test_id);
}

// ################################################################################################
// ################################################################################################

struct s_MyClonableUserdata
{
	lua_Integer val;
};

// ################################################################################################

static int clonable_tostring(lua_State* L)
{
	struct s_MyClonableUserdata* self = (struct s_MyClonableUserdata*) lua_touserdata( L, 1);
	lua_pushfstring( L, "clonable(%d)", self->val);
	return 1;
}

// ################################################################################################

static int clonable_lanesclone( lua_State* L)
{
	// no need to set the metatable, the Lane copying mechanism will take care of it
	struct s_MyClonableUserdata* self = lua_touserdata( L, 1);
	struct s_MyClonableUserdata* to = lua_newuserdata( L, sizeof( struct s_MyClonableUserdata));
	memcpy( to, self, sizeof(struct s_MyClonableUserdata));
	return 1;
}

// ################################################################################################

static luaL_Reg const clonable_mt[] =
{
	{ "__tostring", clonable_tostring},
	//{ "__gc", deep_gc},
	{ "__lanesclone", clonable_lanesclone},
	//{ "set", deep_set},
	{ NULL, NULL }
};

// ################################################################################################

int luaD_new_clonable( lua_State* L)
{
	lua_newuserdata( L, sizeof( struct s_MyClonableUserdata));
	if( luaL_getmetatable( L, "clonable") == LUA_TNIL)      // u mt?
	{
		lua_pop( L, 1);                                       // u
		lua_newtable( L);                                     // u mt
		luaL_setfuncs( L, clonable_mt, 0);
		lua_pushvalue(L, -1);                                 // u mt mt
		lua_setfield(L, -2, "__index");                       // u mt
	}
	lua_setmetatable( L, -2);                               // u
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
	luaL_newlib( L, deep_module);
	return 1;
}
