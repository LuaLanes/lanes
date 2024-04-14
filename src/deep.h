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

#include <atomic>

// forwards
class Universe;

enum class LookupMode
{
    LaneBody, // send the lane body directly from the source to the destination lane
    ToKeeper, // send a function from a lane to a keeper state
    FromKeeper // send a function from a keeper state to a lane
};

enum class DeepOp
{
    New,
    Delete,
    Metatable,
    Module,
};

using luaG_IdFunction = void*(*)(lua_State* L, DeepOp op_);

// ################################################################################################

// xxh64 of string "DEEP_VERSION_3" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey DEEP_VERSION{ 0xB2CC0FD9C0AE9674ull };

// should be used as header for deep userdata
// a deep userdata is a full userdata that stores a single pointer to the actual DeepPrelude-derived object
struct DeepPrelude
{
    UniqueKey const magic{ DEEP_VERSION };
    // when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the idfunc
    luaG_IdFunction idfunc { nullptr };
    // data is destroyed when refcount is 0
    std::atomic<int> m_refcount{ 0 };
};

[[nodiscard]] char const* push_deep_proxy(Dest L, DeepPrelude* prelude, int nuv_, LookupMode mode_);
void free_deep_prelude(lua_State* L, DeepPrelude* prelude_);

LANES_API [[nodiscard]] int luaG_newdeepuserdata(Dest L, luaG_IdFunction idfunc, int nuv_);
LANES_API [[nodiscard]] DeepPrelude* luaG_todeep(lua_State* L, luaG_IdFunction idfunc, int index);
