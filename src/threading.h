#pragma once

#include "platform.h"

#define THREADAPI_WINDOWS 1
#define THREADAPI_PTHREAD 2

#if (defined(PLATFORM_XBOX) || defined(PLATFORM_WIN32) || defined(PLATFORM_POCKETPC))
// #pragma message ( "THREADAPI_WINDOWS" )
#define THREADAPI THREADAPI_WINDOWS
#else // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
// #pragma message ( "THREADAPI_PTHREAD" )
#define THREADAPI THREADAPI_PTHREAD
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

static constexpr int kThreadPrioDefault{ -999 };

// #################################################################################################
// #################################################################################################
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

static constexpr int kThreadPrioMin{ -3 };
static constexpr int kThreadPrioMax{ +3 };

// #################################################################################################
// #################################################################################################
#else // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################

// PThread (Linux, OS X, ...)

// looks like some MinGW installations don't support PTW32_INCLUDE_WINDOWS_H, so let's include it ourselves, just in case
#if defined(PLATFORM_WIN32)
#include <windows.h>
#endif // PLATFORM_WIN32
#include <pthread.h>

#if defined(PLATFORM_LINUX) && !defined(LINUX_SCHED_RR)
static constexpr int kThreadPrioMin{ 0 };
#else
static constexpr int kThreadPrioMin{ -3 };
#endif
static constexpr int kThreadPrioMax{ +3 };

#endif // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################

void THREAD_SETNAME(std::string_view const& name_);
void THREAD_SET_PRIORITY(int prio_, bool sudo_);
void THREAD_SET_AFFINITY(unsigned int aff_);

void THREAD_SET_PRIORITY(std::thread& thread_, int prio_, bool sudo_);
