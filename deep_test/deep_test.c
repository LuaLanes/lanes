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

static int deep_tostring(lua_State* L)
{
	lua_pushliteral( L, "I am a deep_test");
	return 1;
}

// ################################################################################################

static luaL_Reg const deep_mt[] =
{
	{ "__tostring", deep_tostring},
	{ NULL, NULL }
};

// ################################################################################################

static void* deep_test_id( lua_State* L, enum eDeepOp op_)
{
	switch( op_)
	{
		case eDO_new:
		{
			void* allocUD;
			lua_Alloc allocF = lua_getallocf( L, &allocUD);
			void* deep_test = allocF( allocUD, NULL, 0, sizeof(void*));
			return deep_test;
		}

		case eDO_delete:
		{
			void* allocUD;
			lua_Alloc allocF = lua_getallocf( L, &allocUD);
			void* deep_test = lua_touserdata( L, 1);
			allocF( allocUD, deep_test, sizeof(void*), 0);
			return NULL;
		}

		case eDO_metatable:
		{
			lua_newtable( L);
			luaL_setfuncs( L, deep_mt, 0);
			luaG_pushdeepversion( L);
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

int luaD_new_deep(lua_State* L)
{
	return luaG_newdeepuserdata( L, deep_test_id);
}

// ################################################################################################

static luaL_Reg const deep_module[] =
{
	{ "new_deep", luaD_new_deep},
	{ NULL, NULL}
};

// ################################################################################################

extern int __declspec(dllexport) luaopen_deep_test(lua_State* L)
{
	luaL_newlib( L, deep_module);
	return 1;
}
