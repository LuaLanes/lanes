#ifndef __LANES_DEEP_H__
#define __LANES_DEEP_H__ 1

/*
 * public 'deep' API to be used by external modules if they want to implement Lanes-aware userdata
 * said modules will have to link against lanes (it is not really possible to separate the 'deep userdata' implementation from the rest of Lanes)
 */

#include "lua.h"
#include "platform.h"

// forwards
struct s_Universe;
typedef struct s_Universe Universe;

#if !defined LANES_API // when deep is compiled standalone outside Lanes
#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#define LANES_API __declspec(dllexport)
#else
#define LANES_API
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#endif // LANES_API

enum eLookupMode
{
	eLM_LaneBody, // send the lane body directly from the source to the destination lane
	eLM_ToKeeper, // send a function from a lane to a keeper state
	eLM_FromKeeper // send a function from a keeper state to a lane
};
typedef enum eLookupMode LookupMode;

enum eDeepOp
{
	eDO_new,
	eDO_delete,
	eDO_metatable,
	eDO_module,
};
typedef enum eDeepOp DeepOp;

typedef void* (*luaG_IdFunction)( lua_State* L, DeepOp op_);

// ################################################################################################

// this is pointed to by full userdata proxies, and allocated with malloc() to survive any lua_State lifetime
struct s_DeepPrelude
{
	volatile int refcount;
	void* deep;
	// when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the idfunc
	luaG_IdFunction idfunc;
};
typedef struct s_DeepPrelude DeepPrelude;

char const* push_deep_proxy( Universe* U, lua_State* L, DeepPrelude* prelude, LookupMode mode_);
void free_deep_prelude( lua_State* L, DeepPrelude* prelude_);

extern LANES_API int luaG_newdeepuserdata( lua_State* L, luaG_IdFunction idfunc);
extern LANES_API void* luaG_todeep( lua_State* L, luaG_IdFunction idfunc, int index);
extern LANES_API void luaG_pushdeepversion( lua_State* L);

#endif // __LANES_DEEP_H__
