#pragma once

#include "debugspew.h"
#include "macros_and_utils.h"

// forwards
enum class LookupMode;
class Universe;

namespace state {

    void CallOnStateCreate(Universe* U_, lua_State* L_, lua_State* from_, LookupMode mode_);
    [[nodiscard]] lua_State* CreateState(Universe* U_, lua_State* from_);
    void InitializeOnStateCreate(Universe* U_, lua_State* L_);
    [[nodiscard]] lua_State* NewLaneState(Universe* U_, SourceState from_, std::optional<std::string_view> const& libs_);

} // namespace state
