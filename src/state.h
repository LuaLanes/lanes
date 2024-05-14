#pragma once

#include "debugspew.h"
#include "macros_and_utils.h"

// forwards
enum class LookupMode;
class Universe;

void serialize_require(DEBUGSPEW_PARAM_COMMA(Universe* U_) lua_State* L_);

// #################################################################################################

[[nodiscard]] lua_State* create_state(Universe* U_, lua_State* from_);
[[nodiscard]] lua_State* luaG_newstate(Universe* U_, SourceState _from, char const* libs);

// #################################################################################################

void InitializeOnStateCreate(Universe* U_, lua_State* L_);
void CallOnStateCreate(Universe* U_, lua_State* L_, lua_State* from_, LookupMode mode_);
