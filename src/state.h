#pragma once

#include "macros_and_utils.h"

// forwards
enum class LookupMode;
class Universe;

void serialize_require(DEBUGSPEW_PARAM_COMMA(Universe* U) lua_State* L_);

// #################################################################################################

[[nodiscard]] lua_State* create_state(Universe* U, lua_State* from_);
[[nodiscard]] lua_State* luaG_newstate(Universe* U, SourceState _from, char const* libs);

// #################################################################################################

void initialize_on_state_create(Universe* U, lua_State* L_);
void call_on_state_create(Universe* U, lua_State* L_, lua_State* from_, LookupMode mode_);
