#pragma once

#include "unique.hpp"

DECLARE_UNIQUE_TYPE(StackIndex, int);
static_assert(std::is_trivial_v<StackIndex>);

DECLARE_UNIQUE_TYPE(TableIndex, int);
static_assert(std::is_trivial_v<TableIndex>);

DECLARE_UNIQUE_TYPE(UserValueIndex, int);
static_assert(std::is_trivial_v<UserValueIndex>);

DECLARE_UNIQUE_TYPE(UserValueCount, int);
static_assert(std::is_trivial_v<UserValueCount>);

DECLARE_UNIQUE_TYPE(UnusedInt, int);
static_assert(std::is_trivial_v<UnusedInt>);

// #################################################################################################

static constexpr StackIndex kIdxRegistry{ LUA_REGISTRYINDEX };
static constexpr StackIndex kIdxNone{ 0 };
static constexpr StackIndex kIdxTop{ -1 };
