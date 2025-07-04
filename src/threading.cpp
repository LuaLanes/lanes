/*
 * THREADING.CPP                      Copyright (c) 2007-08, Asko Kauppi
 *                                    Copyright (C) 2009-24, Benoit Germain
 *
 * Lua Lanes OS threading specific code.
 *
 * References:
 *      <http://www.cse.wustl.edu/~schmidt/win32-cv-1.html>
 */

/*
===============================================================================

Copyright (C) 2007-10 Asko Kauppi <akauppi@gmail.com>
Copyright (C) 2009-24, Benoit Germain <bnt.germain@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

===============================================================================
*/
#include "_pch.hpp"

#if defined(__linux__)

#ifndef _GNU_SOURCE // definition by the makefile can cause a redefinition error
#define _GNU_SOURCE // must be defined before any include
#endif              // _GNU_SOURCE

#ifdef __ANDROID__
#include <android/log.h>
#define LOG_TAG "LuaLanes"
#endif // __ANDROID__

#endif // __linux__

#include "compat.hpp"
#include "threading.hpp"

#if !defined(PLATFORM_XBOX) && !defined(PLATFORM_WIN32) && !defined(PLATFORM_POCKETPC)
#include <sys/time.h>
#endif // non-WIN32 timing

#if defined(PLATFORM_LINUX) || defined(PLATFORM_CYGWIN)
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef PLATFORM_OSX
#include "threading_osx.h"
#endif

/* Linux with older glibc (such as Debian) don't have pthread_setname_np, but have prctl
 */
#if defined PLATFORM_LINUX
#if defined __GNU_LIBRARY__ && __GLIBC__ >= 2 && __GLIBC_MINOR__ >= 12
#define LINUX_USE_PTHREAD_SETNAME_NP 1
#else // glibc without pthread_setname_np
#include <sys/prctl.h>
#define LINUX_USE_PTHREAD_SETNAME_NP 0
#endif // glibc without pthread_setname_np
#endif // PLATFORM_LINUX

#ifdef _MSC_VER
// ".. selected for automatic inline expansion" (/O2 option)
#pragma warning(disable : 4711)
// ".. type cast from function pointer ... to data pointer"
#pragma warning(disable : 4054)
#endif

static constexpr std::string_view StripFuncName(std::string_view const& where_)
{
    std::string_view funcname_{ where_ };

    auto _args_pos{ funcname_.find_first_of('(') };
    funcname_ = funcname_.substr(0, _args_pos);
    auto _name_pos{ funcname_.find_last_of(' ') };
    funcname_.remove_prefix(_name_pos + 1);
    return funcname_;
}

/*
 * FAIL is for unexpected API return values - essentially programming
 * error in _this_ code.
 */
#if HAVE_WIN32

template <typename F, typename... ARGS>
void Win32Invoke(lua_State* const L_, std::string_view const& where_, F& f_, ARGS... args_)
{
    auto const _ret{ std::invoke(f_, std::forward<ARGS>(args_)...) };
    if (!_ret) {
        auto const _rc{ GetLastError() };
        std::string_view const _funcname{ StripFuncName(where_) };

#if defined(PLATFORM_XBOX)
        luaW_pushstring(L_, "%s() failed with code %d", _funcname.data(), _rc);
#else // PLATFORM_XBOX
        char _buf[256];
        FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM, nullptr, _rc, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), _buf, 256, nullptr);
        luaW_pushstring(L_, "%s() failed with code %d '%s'", _funcname.data(), _rc, _buf);
#endif // PLATFORM_XBOX
        raise_lua_error(L_);
    }
}

#endif // HAVE_WIN32

/*---=== Threading ===---*/

// #################################################################################################
// #################################################################################################
#if THREADAPI == THREADAPI_WINDOWS

static int const gs_prio_remap[] = {
    THREAD_PRIORITY_IDLE,
    THREAD_PRIORITY_LOWEST,
    THREAD_PRIORITY_BELOW_NORMAL,
    THREAD_PRIORITY_NORMAL,
    THREAD_PRIORITY_ABOVE_NORMAL,
    THREAD_PRIORITY_HIGHEST,
    THREAD_PRIORITY_TIME_CRITICAL
};

// #################################################################################################

std::pair<int, int> THREAD_NATIVE_PRIOS()
{
    return std::make_pair(THREAD_PRIORITY_IDLE, THREAD_PRIORITY_TIME_CRITICAL);
}

// #################################################################################################

[[nodiscard]]
void THREAD_SET_PRIORITY(lua_State* const L_, int const prio_, NativePrioFlag const native_, [[maybe_unused]] SudoFlag const sudo_)
{
    // mapped prio range [-3,+3] was checked by the caller
    return Win32Invoke(L_, std::source_location::current().function_name(), SetThreadPriority, GetCurrentThread(), native_ ? prio_ : gs_prio_remap[prio_ + 3]);
}

// #################################################################################################

[[nodiscard]]
void THREAD_SET_PRIORITY(lua_State* const L_, std::thread& thread_, int const prio_, NativePrioFlag const native_, [[maybe_unused]] SudoFlag const sudo_)
{
    // mapped prio range [-3,+3] was checked by the caller
    return Win32Invoke(L_, std::source_location::current().function_name(), SetThreadPriority, thread_.native_handle(), native_ ? prio_ : gs_prio_remap[prio_ + 3]);
}

// #################################################################################################

[[nodiscard]]
void THREAD_SET_AFFINITY(lua_State* const L_, unsigned int aff_)
{
    return Win32Invoke(L_, std::source_location::current().function_name(), SetThreadAffinityMask, GetCurrentThread(), aff_);
}

// #################################################################################################

#if !defined __GNUC__
// see http://msdn.microsoft.com/en-us/library/xcb2z8hs.aspx
#define MS_VC_EXCEPTION 0x406D1388
#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO
{
    DWORD dwType;     // Must be 0x1000.
    LPCSTR szName;    // Pointer to name (in user addr space).
    DWORD dwThreadID; // Thread ID (-1=caller thread).
    DWORD dwFlags;    // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)
#endif // !__GNUC__

void THREAD_SETNAME(std::string_view const& name_)
{
#if !defined __GNUC__
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = name_.data();
    info.dwThreadID = GetCurrentThreadId();
    info.dwFlags = 0;

    __try {
        RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*) &info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#endif // !__GNUC__
}

// #################################################################################################
// #################################################################################################
#else // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################

//  PThread (Linux, OS X, ...)
//
// On OS X, user processes seem to be able to change priorities.
// On Linux, SCHED_RR and su privileges are required..  !-(
//
#include <errno.h>
#include <sched.h>

#if (defined PLATFORM_MINGW) && defined pthread_attr_setschedpolicy
#if pthread_attr_setschedpolicy(A, S) == ENOTSUP
// from the mingw-w64 team:
// Well, we support pthread_setschedparam by which you can specify
// threading-policy. Nevertheless, yes we lack this function. In
// general its implementation is pretty much trivial, as on Win32 target
// just SCHED_OTHER can be supported.
#undef pthread_attr_setschedpolicy
[[nodiscard]]
static int pthread_attr_setschedpolicy(pthread_attr_t* attr, int policy)
{
    if (policy != SCHED_OTHER) {
        return ENOTSUP;
    }
    return 0;
}
#endif // pthread_attr_setschedpolicy()
#endif // defined(__MINGW32__) || defined(__MINGW64__)

template <typename F, typename... ARGS>
void PthreadInvoke(lua_State* const L_, std::string_view const& where_, F& f_, ARGS... args_)
{
    auto const _rc{ std::invoke(f_, std::forward<ARGS>(args_)...) };
    if (_rc) {
        std::string_view const _funcname{ StripFuncName(where_) };

        char const* _why = (_rc == EINVAL) ? "EINVAL"
            : (_rc == EBUSY)              ? "EBUSY"
            : (_rc == EPERM)              ? "EPERM"
            : (_rc == ENOMEM)             ? "ENOMEM"
            : (_rc == ESRCH)              ? "ESRCH"
            : (_rc == ENOTSUP)            ? "ENOTSUP"
                                          : "<UNKNOWN>";

        raise_luaL_error(L_, "%s() failed with code %s", _funcname.data(), _why);
    }
}

// array of 7 thread priority values, hand-tuned by platform so that we offer a uniform [-3,+3] public priority range
static int const gs_prio_remap[] = {
// NB: PThreads priority handling is about as twisty as one can get it
//     (and then some). DON*T TRUST ANYTHING YOU READ ON THE NET!!!

//---
// "Select the scheduling policy for the thread: one of SCHED_OTHER
// (regular, non-real-time scheduling), SCHED_RR (real-time,
// round-robin) or SCHED_FIFO (real-time, first-in first-out)."
//
// "Using the RR policy ensures that all threads having the same
// priority level will be scheduled equally, regardless of their activity."
//
// "For SCHED_FIFO and SCHED_RR, the only required member of the
// sched_param structure is the priority sched_priority. For SCHED_OTHER,
// the affected scheduling parameters are implementation-defined."
//
// "The priority of a thread is specified as a delta which is added to
// the priority of the process."
//
// ".. priority is an integer value, in the range from 1 to 127.
//  1 is the least-favored priority, 127 is the most-favored."
//
// "Priority level 0 cannot be used: it is reserved for the system."
//
// "When you use specify a priority of -99 in a call to
// pthread_setschedparam(), the priority of the target thread is
// lowered to the lowest possible value."
//
// ...

// ** CONCLUSION **
//
// PThread priorities are _hugely_ system specific, and we need at
// least OS specific settings. Hopefully, Linuxes and OS X versions
// are uniform enough, among each other...
//
#if defined PLATFORM_OSX
// AK 10-Apr-07 (OS X PowerPC 10.4.9):
//
// With SCHED_RR, 26 seems to be the "normal" priority, where setting
// it does not seem to affect the order of threads processed.
//
// With SCHED_OTHER, the range 25..32 is normal (maybe the same 26,
// but the difference is not so clear with OTHER).
//
// 'sched_get_priority_min()' and '..max()' give 15, 47 as the
// priority limits. This could imply, user mode applications won't
// be able to use values outside of that range.
//
#define _PRIO_MODE SCHED_OTHER

// OS X 10.4.9 (PowerPC) gives ENOTSUP for process scope
// #define _PRIO_SCOPE PTHREAD_SCOPE_PROCESS

#define _PRIO_HI 32 // seems to work (_carefully_ picked!)
#define _PRIO_0 26  // detected
#define _PRIO_LO 1  // seems to work (tested)

#elif defined PLATFORM_LINUX
// (based on Ubuntu Linux 2.6.15 kernel)
//
// SCHED_OTHER is the default policy, but does not allow for priorities.
// SCHED_RR allows priorities, all of which (1..99) are higher than
// a thread with SCHED_OTHER policy.
//
// <http://kerneltrap.org/node/6080>
// <http://en.wikipedia.org/wiki/Native_POSIX_Thread_Library>
// <http://www.net.in.tum.de/~gregor/docs/pthread-scheduling.html>
//
// Manuals suggest checking #ifdef _POSIX_THREAD_PRIORITY_SCHEDULING,
// but even Ubuntu does not seem to define it.
//
#define _PRIO_MODE SCHED_RR

// NTLP 2.5: only system scope allowed (being the basic reason why
//           root privileges are required..)
// #define _PRIO_SCOPE PTHREAD_SCOPE_PROCESS

#define _PRIO_HI 99
#define _PRIO_0 50
#define _PRIO_LO 1

#elif defined(PLATFORM_BSD)
//
// <http://www.net.in.tum.de/~gregor/docs/pthread-scheduling.html>
//
// "When control over the thread scheduling is desired, then FreeBSD
//  with the libpthread implementation is by far the best choice .."
//
#define _PRIO_MODE SCHED_OTHER
#define _PRIO_SCOPE PTHREAD_SCOPE_PROCESS
#define _PRIO_HI 31
#define _PRIO_0 15
#define _PRIO_LO 1

#elif defined(PLATFORM_CYGWIN) || defined(PLATFORM_MINGW)
//
// TBD: Find right values for Cygwin
//
#define _PRIO_MODE SCHED_OTHER
#define _PRIO_SCOPE PTHREAD_SCOPE_PROCESS
#define _PRIO_HI 15
#define _PRIO_0 7
#define _PRIO_LO 1
#else
#error "Unknown OS: not implemented!"
#endif

#if defined _PRIO_0
#define _PRIO_AN (_PRIO_0 + ((_PRIO_HI - _PRIO_0) / 2))
#define _PRIO_BN (_PRIO_LO + ((_PRIO_0 - _PRIO_LO) / 2))

    _PRIO_LO,
    _PRIO_LO,
    _PRIO_BN,
    _PRIO_0,
    _PRIO_AN,
    _PRIO_HI,
    _PRIO_HI
#endif // _PRIO_0
};

// #################################################################################################

std::pair<int, int> THREAD_NATIVE_PRIOS()
{
    int const _prio_min{ sched_get_priority_min(_PRIO_MODE) };
    int const _prio_max{ sched_get_priority_max(_PRIO_MODE) };
    return std::make_pair(_prio_min, _prio_max);
}

// #################################################################################################

void THREAD_SET_PRIORITY(lua_State* const L_, int const prio_, NativePrioFlag const native_, [[maybe_unused]] SudoFlag const sudo_)
{
#ifdef PLATFORM_LINUX
    if (!sudo_) // only root-privileged process can change priorities
        return;
#endif // PLATFORM_LINUX

    struct sched_param sp;
    // prio range [-3,+3] was checked by the caller
    sp.sched_priority = native_ ? prio_ : gs_prio_remap[prio_ + 3];
    PthreadInvoke(L_, std::source_location::current().function_name(), pthread_setschedparam, pthread_self(), _PRIO_MODE, &sp);
}

// #################################################################################################

void THREAD_SET_PRIORITY(lua_State* const L_, std::thread& thread_, int const prio_, NativePrioFlag const native_, [[maybe_unused]] SudoFlag const sudo_)
{
#ifdef PLATFORM_LINUX
    if (!sudo_) // only root-privileged process can change priorities
        return;
#endif // PLATFORM_LINUX

    struct sched_param sp;
    // prio range [-3,+3] was checked by the caller
    sp.sched_priority = native_ ? prio_ : gs_prio_remap[prio_ + 3];
    PthreadInvoke(L_, std::source_location::current().function_name(), pthread_setschedparam, thread_.native_handle(), _PRIO_MODE, &sp);
}

// #################################################################################################

#ifdef __PROSPERO__

void THREAD_SET_AFFINITY(lua_State* const L_, unsigned int aff_)
{
    PthreadInvoke(L_, std::source_location::current().function_name(), scePthreadSetaffinity, scePthreadSelf(), aff_);
}

#else // __PROSPERO__

void THREAD_SET_AFFINITY(lua_State* const L_, unsigned int aff_)
{
#if HAVE_WIN32 // "hybrid": Win32 API is available, and pthread too
    // since pthread_setaffinity_np can be missing (for example mingw), use win32 api instead
    Win32Invoke(L_, std::source_location::current().function_name(), SetThreadAffinityMask, GetCurrentThread(), aff_);
#else // pure pthread
    int bit = 0;
#ifdef __NetBSD__
    cpuset_t* cpuset = cpuset_create();
    if (cpuset == nullptr)
        _PT_FAIL(errno, "cpuset_create", __FILE__, __LINE__ - 2);
#define CPU_SET(b, s) cpuset_set(b, *(s))
#else
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
#endif
    while (aff_ != 0) {
        if (aff_ & 1) {
            CPU_SET(bit, &cpuset);
        }
        ++bit;
        aff_ >>= 1;
    }
#ifdef __ANDROID__
    
    PthreadInvoke(L_, std::source_location::current().function_name(), sched_setaffinity, pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(__NetBSD__)
    PthreadInvoke(L_, std::source_location::current().function_name(), pthread_setaffinity_np, pthread_self(), cpuset_size(cpuset), cpuset);
    cpuset_destroy(cpuset);
#else
    PthreadInvoke(L_, std::source_location::current().function_name(), pthread_setaffinity_np, pthread_self(), sizeof(cpu_set_t), &cpuset);
#endif
#endif // PLATFORM_MINGW
}

#endif // __PROSPERO__

// #################################################################################################

#ifdef __PROSPERO__

void THREAD_SETNAME(std::string_view const& name_)
{
    scePthreadRename(scePthreadSelf(), name_.data());
}

#else // __PROSPERO__

void THREAD_SETNAME(std::string_view const& name_)
{
    // exact API to set the thread name is platform-dependent
    // if you need to fix the build, or if you know how to fill a hole, tell me (bnt.germain@gmail.com) so that I can submit the fix in github.
#if defined PLATFORM_MINGW
    pthread_setname_np(pthread_self(), name_.data());
#elif defined PLATFORM_BSD && !defined __NetBSD__
    pthread_set_name_np(pthread_self(), name_.data());
#elif defined PLATFORM_BSD && defined __NetBSD__
    pthread_setname_np(pthread_self(), "%s", (void*) name_.data());
#elif defined PLATFORM_LINUX
#if LINUX_USE_PTHREAD_SETNAME_NP
    pthread_setname_np(pthread_self(), name_.data());
#else  // LINUX_USE_PTHREAD_SETNAME_NP
    prctl(PR_SET_NAME, name_.data(), 0, 0, 0);
#endif // LINUX_USE_PTHREAD_SETNAME_NP
#elif defined PLATFORM_QNX || defined PLATFORM_CYGWIN
    pthread_setname_np(pthread_self(), name_.data());
#elif defined PLATFORM_OSX
    pthread_setname_np(name_.data());
#else
    fprintf(stderr, "THREAD_SETNAME: unsupported platform\n");
    abort();
#endif
}

#endif // __PROSPERO__

#endif // THREADAPI == THREADAPI_PTHREAD
// #################################################################################################
// #################################################################################################
