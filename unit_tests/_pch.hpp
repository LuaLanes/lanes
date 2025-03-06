#pragma once

#include <atomic>
#include <cassert>
#include <filesystem>
#include <source_location>
#include <mutex>
#include <thread>
#include <variant>

#include "catch_amalgamated.hpp"

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
