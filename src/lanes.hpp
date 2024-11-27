#pragma once

#include "lanesconf.h"

#define LANES_VERSION_MAJOR 4
#define LANES_VERSION_MINOR 0
#define LANES_VERSION_PATCH 0

#define LANES_MIN_VERSION_REQUIRED(MAJOR, MINOR, PATCH) ((LANES_VERSION_MAJOR > MAJOR) || (LANES_VERSION_MAJOR == MAJOR && (LANES_VERSION_MINOR > MINOR || (LANES_VERSION_MINOR == MINOR && LANES_VERSION_PATCH >= PATCH))))
#define LANES_VERSION_LESS_THAN(MAJOR, MINOR, PATCH) ((LANES_VERSION_MAJOR < MAJOR) || (LANES_VERSION_MAJOR == MAJOR && (LANES_VERSION_MINOR < MINOR || (LANES_VERSION_MINOR == MINOR && LANES_VERSION_PATCH < PATCH))))
#define LANES_VERSION_LESS_OR_EQUAL(MAJOR, MINOR, PATCH) ((LANES_VERSION_MAJOR < MAJOR) || (LANES_VERSION_MAJOR == MAJOR && (LANES_VERSION_MINOR < MINOR || (LANES_VERSION_MINOR == MINOR && LANES_VERSION_PATCH <= PATCH))))
#define LANES_VERSION_GREATER_THAN(MAJOR, MINOR, PATCH) ((LANES_VERSION_MAJOR > MAJOR) || (LANES_VERSION_MAJOR == MAJOR && (LANES_VERSION_MINOR > MINOR || (LANES_VERSION_MINOR == MINOR && LANES_VERSION_PATCH > PATCH))))
#define LANES_VERSION_GREATER_OR_EQUAL(MAJOR, MINOR, PATCH) ((LANES_VERSION_MAJOR > MAJOR) || (LANES_VERSION_MAJOR == MAJOR && (LANES_VERSION_MINOR > MINOR || (LANES_VERSION_MINOR == MINOR && LANES_VERSION_PATCH >= PATCH))))

LANES_API int luaopen_lanes_core(lua_State* L_);

// Call this to work with embedded Lanes instead of calling luaopen_lanes_core()
LANES_API void luaopen_lanes_embedded(lua_State* L_, lua_CFunction luaopen_lanes_);
using luaopen_lanes_embedded_t = void (*)(lua_State* L_, lua_CFunction luaopen_lanes_);
static_assert(std::is_same_v<decltype(&luaopen_lanes_embedded), luaopen_lanes_embedded_t>, "signature changed: check all uses of luaopen_lanes_embedded_t");

LANES_API int lanes_register(lua_State* L_);
