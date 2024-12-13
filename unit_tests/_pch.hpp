#pragma once

#include <filesystem>
#include <source_location>
#include <mutex>
#include <variant>

#include "gtest/gtest.h"

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "lanes/src/allocator.hpp"
#include "lanes/src/compat.hpp"
#include "lanes/src/universe.hpp"
