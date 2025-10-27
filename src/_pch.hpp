#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <chrono>
#include <compare>
#include <concepts>
#include <condition_variable>
#include <cstring>
#include <format>
#include <functional>
#include <iostream>
#ifndef __PROSPERO__
#include <latch>
#endif // __PROSPERO__
#include <mutex>
#include <optional>
#include <ranges>
#include <source_location>
//#include <stop_token>
#include <span>
#include <string_view>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#ifdef _MSC_VER

// warning level /Wall triggers a bunch of warnings in Lua headers. we can't do anything about that, so suppress them
#pragma warning(push)
#pragma warning(disable : 4820) // 'n' bytes padding added after data member 'x'

#endif // _MSC_VER

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

#ifdef _MSC_VER

#pragma warning(pop)

#pragma warning(disable : 4061) // enumerator 'x' in switch of 'y' is not explicitly handled by a case label
#pragma warning(disable : 4514) // 'x': unreferenced inline function has been removed
#pragma warning(disable : 4623) // 'x': default constructor was implicitly defined as deleted
#pragma warning(disable : 4623) // 'x': default constructor was implicitly defined as deleted
#pragma warning(disable : 4625) // 'x': copy constructor was implicitly defined as deleted
#pragma warning(disable : 4626) // 'x': assignment operator was implicitly defined as deleted
#pragma warning(disable : 4820) // 'n' bytes padding added after data member 'x'
#pragma warning(disable : 5026) // 'x': move constructor was implicitly defined as deleted
#pragma warning(disable : 5027) // 'x': move assignment operator was implicitly defined as deleted
#pragma warning(disable : 5039) // 'x': pointer or reference to potentially throwing function passed to 'extern "C"' function under -EHc. Undefined behavior may occur if this function throws an exception.
#pragma warning(disable : 5045) // Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
#pragma warning(disable : 5246) // 'x': the initialization of a subobject should be wrapped in braces

#endif // _MSC_VER
