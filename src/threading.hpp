#pragma once

#include "platform.h"
#include "unique.hpp"

#define THREADAPI_WINDOWS 1
#define THREADAPI_PTHREAD 2

#if __has_include(<pthread.h>)
#include <pthread.h>
#define HAVE_PTHREAD 1
//#pragma message("HAVE_PTHREAD")
#else
#define HAVE_PTHREAD 0
#endif // <pthread.h>

#if __has_include(<windows.h>)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define HAVE_WIN32 1
//#pragma message("HAVE_WIN32")
#elif __has_include(<xtl.h>)
#include <xtl.h>
#define HAVE_WIN32 1
//#pragma message("HAVE_WIN32")
#else // no <windows.h> nor <xtl.h>
#define HAVE_WIN32 0
#endif // <windows.h>

#if HAVE_PTHREAD
// unless proven otherwise, if pthread is available, let's assume that's what std::thread is using
#define THREADAPI THREADAPI_PTHREAD
//#pragma message ( "THREADAPI_PTHREAD" )
#elif HAVE_WIN32
//#pragma message ( "THREADAPI_WINDOWS" )
#define THREADAPI THREADAPI_WINDOWS
#include <process.h>
#else // unknown
#error "unsupported threading API"
#endif // unknown

static constexpr int kThreadPrioDefault{ -999 };

// #################################################################################################
// #################################################################################################
#if THREADAPI == THREADAPI_WINDOWS


/*
#define XSTR(x) STR(x)
#define STR(x) #x
#pragma message( "The value of _WIN32_WINNT: " XSTR(_WIN32_WINNT))
*/

static constexpr int kThreadPrioMin{ -3 };
static constexpr int kThreadPrioMax{ +3 };

// #################################################################################################
// #################################################################################################
#else // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################

// PThread (Linux, OS X, ...)

#if defined(PLATFORM_LINUX) && !defined(LINUX_SCHED_RR)
static constexpr int kThreadPrioMin{ 0 };
#else
static constexpr int kThreadPrioMin{ -3 };
#endif
static constexpr int kThreadPrioMax{ +3 };

#endif // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################

DECLARE_UNIQUE_TYPE(SudoFlag, bool);
DECLARE_UNIQUE_TYPE(NativePrioFlag, bool);

std::pair<int, int> THREAD_NATIVE_PRIOS();

void THREAD_SETNAME(std::string_view const& name_);

void THREAD_SET_PRIORITY(lua_State* L_, int prio_, NativePrioFlag native_, SudoFlag sudo_);

void THREAD_SET_AFFINITY(lua_State* L_, unsigned int aff_);

void THREAD_SET_PRIORITY(lua_State* L_, std::thread& thread_, int prio_, NativePrioFlag native_, SudoFlag sudo_);
