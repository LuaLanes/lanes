#pragma once

#include "unique.hpp"

DECLARE_UNIQUE_TYPE(StackIndex, int);
static_assert(std::is_trivial_v<StackIndex>);

// #################################################################################################

static constexpr StackIndex kIdxRegistry{ LUA_REGISTRYINDEX };
static constexpr StackIndex kIdxTop{ -1 };
