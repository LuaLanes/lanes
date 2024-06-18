#pragma once

#include "platform.h"

// here is a place that's as good as any other to list a few coding conventions that I will endeavour to follow:
//
// indentation:
// ------------
// spaces everywhere
// case indented inside switch braces
// accessibility keywords indented inside class braces
// matching braces have the same indentation
//
// identifiers:
// ------------
// style is camel case. scope of variable is optionally specified with a single lowercase letter.
// constants: prefix k, followed by an uppercase letter
// program-level global variable: in 'global' namespace, prefix g, followed by an uppercase letter
// file-level types: in anonymous namespace
// file-level static variable: in anonymous::'local' namespace, prefix s, followed by an uppercase letter
// file-level function (static or not): no prefix, start with an uppercase letter
// class/struct/enum type: no prefix, start with an uppercase letter
// static class member/method: no prefix, start with an uppercase letter
// regular class member/method: no prefix, start with a lowercase letter
// function argument: suffix _
// static function variable: prefix s, followed by an uppercase letter
// function local variable: prefix _, followed by an uppercase letter
// named lambda capture: no prefix, start with a lowercase letter
// stuff for external consumption in a 'lanes' namespace

#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
#ifdef __cplusplus
#define LANES_API extern "C" __declspec(dllexport)
#else
#define LANES_API extern __declspec(dllexport)
#endif // __cplusplus
#else
#ifdef __cplusplus
#define LANES_API extern "C"
#else
#define LANES_API extern
#endif // __cplusplus
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

// kind of MSVC-specific
#ifdef _DEBUG
#define HAVE_LUA_ASSERT() 1
#else // NDEBUG
#define HAVE_LUA_ASSERT() 0
#endif // NDEBUG

#define USE_DEBUG_SPEW() 0
#define HAVE_DECODA_SUPPORT() 0
