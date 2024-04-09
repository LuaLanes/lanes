#pragma once

#include "platform.h"

#include <time.h>
#include <thread>

#define THREADAPI_WINDOWS 1
#define THREADAPI_PTHREAD 2

#if( defined( PLATFORM_XBOX) || defined( PLATFORM_WIN32) || defined( PLATFORM_POCKETPC))
//#pragma message ( "THREADAPI_WINDOWS" )
#define THREADAPI THREADAPI_WINDOWS
#else // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
//#pragma message ( "THREADAPI_PTHREAD" )
#define THREADAPI THREADAPI_PTHREAD
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

static constexpr int THREAD_PRIO_DEFAULT{ -999 };

// ##################################################################################################
// ##################################################################################################
#if THREADAPI == THREADAPI_WINDOWS

#if defined(PLATFORM_XBOX)
#include <xtl.h>
#else // !PLATFORM_XBOX
#define WIN32_LEAN_AND_MEAN
// CONDITION_VARIABLE needs version 0x0600+
// _WIN32_WINNT value is already defined by MinGW, but not by MSVC
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif // _WIN32_WINNT
#include <windows.h>
#endif // !PLATFORM_XBOX
#include <process.h>

/*
#define XSTR(x) STR(x)
#define STR(x) #x
#pragma message( "The value of _WIN32_WINNT: " XSTR(_WIN32_WINNT))
*/

static constexpr int THREAD_PRIO_MIN{ -3 };
static constexpr int THREAD_PRIO_MAX{ +3 };

// ##################################################################################################
// ##################################################################################################
#else // THREADAPI == THREADAPI_PTHREAD
// ##################################################################################################
// ##################################################################################################

// PThread (Linux, OS X, ...)

// looks like some MinGW installations don't support PTW32_INCLUDE_WINDOWS_H, so let's include it ourselves, just in case
#if defined(PLATFORM_WIN32)
#include <windows.h>
#endif // PLATFORM_WIN32
#include <pthread.h>

// Yield is non-portable:
//
//    OS X 10.4.8/9 has pthread_yield_np()
//    Linux 2.4   has pthread_yield() if _GNU_SOURCE is #defined
//    FreeBSD 6.2 has pthread_yield()
//    ...
//

#if defined(PLATFORM_LINUX)
extern volatile bool sudo;
#   ifdef LINUX_SCHED_RR
#       define THREAD_PRIO_MIN (sudo ? -3 : 0)
#   else
static constexpr int THREAD_PRIO_MIN{ 0 };
#endif
#       define THREAD_PRIO_MAX (sudo ? +3 : 0)
#else
static constexpr int THREAD_PRIO_MIN{ -3 };
static constexpr int THREAD_PRIO_MAX{ +3 };
#endif

#endif // THREADAPI == THREADAPI_WINDOWS
// ##################################################################################################
// ##################################################################################################

void THREAD_SETNAME(char const* _name);
void THREAD_SET_PRIORITY(int prio);
void THREAD_SET_AFFINITY(unsigned int aff);

void JTHREAD_SET_PRIORITY(std::jthread& thread_, int prio_);
