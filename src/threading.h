#pragma once

#include "platform.h"

#include <time.h>
#include <thread>

/* Note: ERROR is a defined entity on Win32
  PENDING: The Lua VM hasn't done anything yet.
  RUNNING, WAITING: Thread is inside the Lua VM. If the thread is forcefully stopped, we can't lua_close() the Lua State.
  DONE, ERROR_ST, CANCELLED: Thread execution is outside the Lua VM. It can be lua_close()d.
*/
enum e_status { PENDING, RUNNING, WAITING, DONE, ERROR_ST, CANCELLED };

#define THREADAPI_WINDOWS 1
#define THREADAPI_PTHREAD 2

#if( defined( PLATFORM_XBOX) || defined( PLATFORM_WIN32) || defined( PLATFORM_POCKETPC))
//#pragma message ( "THREADAPI_WINDOWS" )
#define THREADAPI THREADAPI_WINDOWS
#else // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
//#pragma message ( "THREADAPI_PTHREAD" )
#define THREADAPI THREADAPI_PTHREAD
#endif // (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)

/*---=== Locks & Signals ===---
*/

#if THREADAPI == THREADAPI_WINDOWS
  #if defined( PLATFORM_XBOX)
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

  // MSDN: http://msdn2.microsoft.com/en-us/library/ms684254.aspx
  //
  // CRITICAL_SECTION can be used for simple code protection. Mutexes are
  // needed for use with the SIGNAL system.
  //

    #if _WIN32_WINNT < 0x0600 // CONDITION_VARIABLE aren't available, use a signal

    struct SIGNAL_T
    {
        CRITICAL_SECTION    signalCS;
        CRITICAL_SECTION    countCS;
        HANDLE      waitEvent;
        HANDLE      waitDoneEvent;
        LONG        waitersCount;
    };


    #else // CONDITION_VARIABLE are available, use them

    #define SIGNAL_T CONDITION_VARIABLE
    #define MUTEX_INIT( ref) InitializeCriticalSection( ref)
    #define MUTEX_FREE( ref) DeleteCriticalSection( ref)
    #define MUTEX_LOCK( ref) EnterCriticalSection( ref)
    #define MUTEX_UNLOCK( ref) LeaveCriticalSection( ref)

    #endif // CONDITION_VARIABLE are available

  #define MUTEX_RECURSIVE_INIT(ref)  MUTEX_INIT(ref)  /* always recursive in Win32 */

  using THREAD_RETURN_T = unsigned int;

  #define YIELD() Sleep(0)
    #define THREAD_CALLCONV __stdcall
#else // THREADAPI == THREADAPI_PTHREAD
  // PThread (Linux, OS X, ...)

  // looks like some MinGW installations don't support PTW32_INCLUDE_WINDOWS_H, so let's include it ourselves, just in case
  #if defined(PLATFORM_WIN32)
  #include <windows.h>
  #endif // PLATFORM_WIN32
  #include <pthread.h>

  #ifdef PLATFORM_LINUX
    #if defined(__GLIBC__)
      # define _MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE_NP
    #else
      # define _MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE
    #endif
  #else
    /* OS X, ... */
  # define _MUTEX_RECURSIVE PTHREAD_MUTEX_RECURSIVE
  #endif

  #define MUTEX_INIT(ref)    pthread_mutex_init(ref, nullptr)
  #define MUTEX_RECURSIVE_INIT(ref) \
      { pthread_mutexattr_t a; pthread_mutexattr_init( &a ); \
        pthread_mutexattr_settype( &a, _MUTEX_RECURSIVE ); \
        pthread_mutex_init(ref,&a); pthread_mutexattr_destroy( &a ); \
      }
  #define MUTEX_FREE(ref)    pthread_mutex_destroy(ref)
  #define MUTEX_LOCK(ref)    pthread_mutex_lock(ref)
  #define MUTEX_UNLOCK(ref)  pthread_mutex_unlock(ref)

  using THREAD_RETURN_T = void *;

  using SIGNAL_T = pthread_cond_t;

  // Yield is non-portable:
  //
  //    OS X 10.4.8/9 has pthread_yield_np()
  //    Linux 2.4   has pthread_yield() if _GNU_SOURCE is #defined
  //    FreeBSD 6.2 has pthread_yield()
  //    ...
  //
  #if defined( PLATFORM_OSX)
    #define YIELD() pthread_yield_np()
  #else
    #define YIELD() sched_yield()
  #endif
    #define THREAD_CALLCONV
#endif //THREADAPI == THREADAPI_PTHREAD

/*
* 'time_d': <0.0 for no timeout
*           0.0 for instant check
*           >0.0 absolute timeout in secs + ms
*/
using time_d = double;
time_d now_secs(void);

/*---=== Threading ===---
*/

#define THREAD_PRIO_DEFAULT (-999)

#if THREADAPI == THREADAPI_WINDOWS

#	define THREAD_PRIO_MIN (-3)
#	define THREAD_PRIO_MAX (+3)

#else // THREADAPI == THREADAPI_PTHREAD

    /* Platforms that have a timed 'pthread_join()' can get away with a simpler
     * implementation. Others will use a condition variable.
     */
#	if defined __WINPTHREADS_VERSION
//#		define USE_PTHREAD_TIMEDJOIN
#	endif // __WINPTHREADS_VERSION

# ifdef USE_PTHREAD_TIMEDJOIN
#  ifdef PLATFORM_OSX
#   error "No 'pthread_timedjoin()' on this system"
#  else
    /* Linux, ... */
#   define PTHREAD_TIMEDJOIN pthread_timedjoin_np
#  endif
# endif

#	if defined(PLATFORM_LINUX)
        extern volatile bool sudo;
#		ifdef LINUX_SCHED_RR
#			define THREAD_PRIO_MIN (sudo ? -3 : 0)
#		else
#			define THREAD_PRIO_MIN (0)
#		endif
#		define THREAD_PRIO_MAX (sudo ? +3 : 0)
#	else
#		define THREAD_PRIO_MIN (-3)
#		define THREAD_PRIO_MAX (+3)
#	endif

#endif // THREADAPI == THREADAPI_WINDOWS

/*
* Win32 and PTHREAD_TIMEDJOIN allow waiting for a thread with a timeout.
* Posix without PTHREAD_TIMEDJOIN needs to use a condition variable approach.
*/
#define THREADWAIT_TIMEOUT 1
#define THREADWAIT_CONDVAR 2

#if THREADAPI == THREADAPI_WINDOWS || (defined PTHREAD_TIMEDJOIN)
#define THREADWAIT_METHOD THREADWAIT_TIMEOUT
#else // THREADAPI == THREADAPI_WINDOWS || (defined PTHREAD_TIMEDJOIN)
#define THREADWAIT_METHOD THREADWAIT_CONDVAR
#endif // THREADAPI == THREADAPI_WINDOWS || (defined PTHREAD_TIMEDJOIN)


void THREAD_SETNAME( char const* _name);
void THREAD_SET_PRIORITY( int prio);
void THREAD_SET_AFFINITY( unsigned int aff);

void JTHREAD_SET_PRIORITY(std::jthread& thread_, int prio_);
