#pragma once

#include "unique.hpp"

DECLARE_UNIQUE_TYPE(StackIndex, int);
static_assert(std::is_trivial_v<StackIndex>);

DECLARE_UNIQUE_TYPE(UserValueCount, int);
static_assert(std::is_trivial_v<UserValueCount>);

DECLARE_UNIQUE_TYPE(UnusedInt, int);
static_assert(std::is_trivial_v<UserValueCount>);

// #################################################################################################

static constexpr StackIndex kIdxRegistry{ LUA_REGISTRYINDEX };
static constexpr StackIndex kIdxTop{ -1 };
