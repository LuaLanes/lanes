#pragma once

/*
 * public 'deep' API to be used by external modules if they want to implement Lanes-aware userdata
 * said modules will have to link against lanes (it is not really possible to separate the 'deep userdata' implementation from the rest of Lanes)
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "lanesconf.h"
#include "uniquekey.h"

// forwards
struct Universe;

enum LookupMode
{
    eLM_LaneBody, // send the lane body directly from the source to the destination lane
    eLM_ToKeeper, // send a function from a lane to a keeper state
    eLM_FromKeeper // send a function from a keeper state to a lane
};

enum DeepOp
{
    eDO_new,
    eDO_delete,
    eDO_metatable,
    eDO_module,
};

using luaG_IdFunction = void*( lua_State* L, DeepOp op_);

// ################################################################################################

// fnv164 of string "DEEP_VERSION_2" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey DEEP_VERSION{ 0xB4B0119C10642B29ull };

// should be used as header for full userdata
struct DeepPrelude
{
    UniqueKey const magic{ DEEP_VERSION };
    // when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the idfunc
    luaG_IdFunction* idfunc { nullptr };
    // data is destroyed when refcount is 0
    volatile int refcount{ 0 };
};

char const* push_deep_proxy( Universe* U, lua_State* L, DeepPrelude* prelude, int nuv_, LookupMode mode_);
void free_deep_prelude( lua_State* L, DeepPrelude* prelude_);

LANES_API int luaG_newdeepuserdata( lua_State* L, luaG_IdFunction* idfunc, int nuv_);
LANES_API void* luaG_todeep( lua_State* L, luaG_IdFunction* idfunc, int index);
