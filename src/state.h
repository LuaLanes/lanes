#pragma once

#include "debugspew.h"
#include "macros_and_utils.h"

// forwards
enum class LookupMode;
class Universe;

void serialize_require(lua_State* L_);

// #################################################################################################

[[nodiscard]] lua_State* create_state(Universe* U_, lua_State* from_);
[[nodiscard]] lua_State* luaG_newstate(Universe* U_, SourceState from_, std::optional<std::string_view> const& libs_);

// #################################################################################################

void InitializeOnStateCreate(Universe* U_, lua_State* L_);
void CallOnStateCreate(Universe* U_, lua_State* L_, lua_State* from_, LookupMode mode_);
