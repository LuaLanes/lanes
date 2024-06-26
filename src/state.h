#pragma once

#include "debugspew.h"
#include "macros_and_utils.h"

// forwards
enum class LookupMode;
class Universe;

namespace state {
    [[nodiscard]] lua_State* CreateState(Universe* U_, lua_State* from_, std::string_view const& hint_);
    [[nodiscard]] lua_State* NewLaneState(Universe* U_, SourceState from_, std::optional<std::string_view> const& libs_);
    LUAG_FUNC(supported_libs);
} // namespace state
