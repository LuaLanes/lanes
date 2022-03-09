/*
 * LANES.C                              Copyright (c) 2007-08, Asko Kauppi
 *                                      Copyright (C) 2009-19, Benoit Germain
 *
 * Multithreading in Lua.
 * 
 * History:
 *      See CHANGES
 *
 * Platforms (tested internally):
 *      OS X (10.5.7 PowerPC/Intel)
 *      Linux x86 (Ubuntu 8.04)
 *      Win32 (Windows XP Home SP2, Visual C++ 2005/2008 Express)
 *
 * Platforms (tested externally):
 *      Win32 (MSYS) by Ross Berteig.
 *
 * Platforms (testers appreciated):
 *      Win64 - should work???
 *      Linux x64 - should work
 *      FreeBSD - should work
 *      QNX - porting shouldn't be hard
 *      Sun Solaris - porting shouldn't be hard
 *
 * References:
 *      "Porting multithreaded applications from Win32 to Mac OS X":
 *      <http://developer.apple.com/macosx/multithreadedprogramming.html>
 *
 *      Pthreads:
 *      <http://vergil.chemistry.gatech.edu/resources/programming/threads.html>
 *
 *      MSDN: <http://msdn2.microsoft.com/en-us/library/ms686679.aspx>
 *
 *      <http://ridiculousfish.com/blog/archives/2007/02/17/barrier>
 *
 * Defines:
 *      -DLINUX_SCHED_RR: all threads are lifted to SCHED_RR category, to
 *          allow negative priorities [-3,-1] be used. Even without this,
 *          using priorities will require 'sudo' privileges on Linux.
 *
 *      -DUSE_PTHREAD_TIMEDJOIN: use 'pthread_timedjoin_np()' for waiting
 *          for threads with a timeout. This changes the thread cleanup
 *          mechanism slightly (cleans up at the join, not once the thread
 *          has finished). May or may not be a good idea to use it.
 *          Available only in selected operating systems (Linux).
 *
 * Bugs:
 *
 * To-do:
 *
 * Make waiting threads cancellable.
 *      ...
 */

/*
===============================================================================

Copyright (C) 2007-10 Asko Kauppi <akauppi@gmail.com>
              2011-19 Benoit Germain <bnt.germain@gmail.com>

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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <assert.h>

#include "lanes.h"
#include "threading.h"
#include "compat.h"
#include "tools.h"
#include "state.h"
#include "universe.h"
#include "keeper.h"
#include "lanes_private.h"

#if !(defined( PLATFORM_XBOX) || defined( PLATFORM_WIN32) || defined( PLATFORM_POCKETPC))
# include <sys/time.h>
#endif

/* geteuid() */
#ifdef PLATFORM_LINUX
# include <unistd.h>
# include <sys/types.h>
#endif

/* Do you want full call stacks, or just the line where the error happened?
*
* TBD: The full stack feature does not seem to work (try 'make error').
*/
#define ERROR_FULL_STACK 1 // must be either 0 or 1 as we do some index arithmetics with it!

// intern the debug name in the specified lua state so that the pointer remains valid when the lane's state is closed
static void securize_debug_threadname( lua_State* L, Lane* s)
{
    STACK_CHECK( L, 0);
    STACK_GROW( L, 3);
    lua_getiuservalue( L, 1, 1);
    lua_newtable( L);
    // Lua 5.1 can't do 's->debug_name = lua_pushstring( L, s->debug_name);'
    lua_pushstring( L, s->debug_name);
    s->debug_name = lua_tostring( L, -1);
    lua_rawset( L, -3);
    lua_pop( L, 1);
    STACK_END( L, 0);
}

#if ERROR_FULL_STACK
static int lane_error( lua_State* L);
// crc64/we of string "STACKTRACE_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( STACKTRACE_REGKEY, 0x534af7d3226a429f);
#endif // ERROR_FULL_STACK

/*
* registry[FINALIZER_REG_KEY] is either nil (no finalizers) or a table
* of functions that Lanes will call after the executing 'pcall' has ended.
*
* We're NOT using the GC system for finalizer mainly because providing the
* error (and maybe stack trace) parameters to the finalizer functions would
* anyways complicate that approach.
*/
// crc64/we of string "FINALIZER_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( FINALIZER_REGKEY, 0x188fccb8bf348e09);

struct s_Linda;

/*
* Push a table stored in registry onto Lua stack.
*
* If there is no existing table, create one if 'create' is TRUE.
* 
* Returns: TRUE if a table was pushed
*          FALSE if no table found, not created, and nothing pushed
*/
static bool_t push_registry_table( lua_State* L, UniqueKey key, bool_t create)
{
    STACK_GROW( L, 3);
    STACK_CHECK( L, 0);

    REGISTRY_GET( L, key);                                                       // ?
    if( lua_isnil( L, -1))                                                       // nil?
    {
        lua_pop( L, 1);                                                            //

        if( !create)
        {
            return FALSE;
        }

        lua_newtable( L);                                                          // t
        REGISTRY_SET( L, key, lua_pushvalue( L, -2));
    }
    STACK_END( L, 1);
    return TRUE;    // table pushed
}

#if HAVE_LANE_TRACKING()

// The chain is ended by '(Lane*)(-1)', not NULL:
// 'tracking_first -> ... -> ... -> (-1)'
#define TRACKING_END ((Lane *)(-1))

/*
 * Add the lane to tracking chain; the ones still running at the end of the
 * whole process will be cancelled.
 */
static void tracking_add( Lane* s)
{

    MUTEX_LOCK( &s->U->tracking_cs);
    {
        assert( s->tracking_next == NULL);

        s->tracking_next = s->U->tracking_first;
        s->U->tracking_first = s;
    }
    MUTEX_UNLOCK( &s->U->tracking_cs);
}

/*
 * A free-running lane has ended; remove it from tracking chain
 */
static bool_t tracking_remove( Lane* s)
{
    bool_t found = FALSE;
    MUTEX_LOCK( &s->U->tracking_cs);
    {
        // Make sure (within the MUTEX) that we actually are in the chain
        // still (at process exit they will remove us from chain and then
        // cancel/kill).
        //
        if( s->tracking_next != NULL)
        {
            Lane** ref = (Lane**) &s->U->tracking_first;

            while( *ref != TRACKING_END)
            {
                if( *ref == s)
                {
                    *ref = s->tracking_next;
                    s->tracking_next = NULL;
                    found = TRUE;
                    break;
                }
                ref = (Lane**) &((*ref)->tracking_next);
            }
            assert( found);
        }
    }
    MUTEX_UNLOCK( &s->U->tracking_cs);
    return found;
}

#endif // HAVE_LANE_TRACKING()

//---
// low-level cleanup

static void lane_cleanup( Lane* s)
{
    // Clean up after a (finished) thread
    //
#if THREADWAIT_METHOD == THREADWAIT_CONDVAR
    SIGNAL_FREE( &s->done_signal);
    MUTEX_FREE( &s->done_lock);
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR

#if HAVE_LANE_TRACKING()
    if( s->U->tracking_first != NULL)
    {
        // Lane was cleaned up, no need to handle at process termination
        tracking_remove( s);
    }
#endif // HAVE_LANE_TRACKING()

    // don't hijack the state allocator when running LuaJIT because it looks like LuaJIT does not expect it and might invalidate the memory unexpectedly
#if USE_LUA_STATE_ALLOCATOR()
    {
        AllocatorDefinition* const allocD = &s->U->protected_allocator.definition;
        allocD->allocF(allocD->allocUD, s, sizeof(Lane), 0);
    }
#else // USE_LUA_STATE_ALLOCATOR()
    free(s);
#endif // USE_LUA_STATE_ALLOCATOR()
}

/*
 * ###############################################################################################
 * ########################################## Finalizer ##########################################
 * ###############################################################################################
 */

//---
// void= finalizer( finalizer_func )
//
// finalizer_func( [err, stack_tbl] )
//
// Add a function that will be called when exiting the lane, either via
// normal return or an error.
//
LUAG_FUNC( set_finalizer)
{
    luaL_argcheck( L, lua_isfunction( L, 1), 1, "finalizer should be a function");
    luaL_argcheck( L, lua_gettop( L) == 1, 1, "too many arguments");
    // Get the current finalizer table (if any)
    push_registry_table( L, FINALIZER_REGKEY, TRUE /*do create if none*/);      // finalizer {finalisers}
    STACK_GROW( L, 2);
    lua_pushinteger( L, lua_rawlen( L, -1) + 1);                                 // finalizer {finalisers} idx
    lua_pushvalue( L, 1);                                                        // finalizer {finalisers} idx finalizer
    lua_rawset( L, -3);                                                          // finalizer {finalisers}
    lua_pop( L, 2);                                                              //
    return 0;
}


//---
// Run finalizers - if any - with the given parameters
//
// If 'rc' is nonzero, error message and stack index (the latter only when ERROR_FULL_STACK == 1) are available as:
//      [-1]: stack trace (table)
//      [-2]: error message (any type)
//
// Returns:
//      0 if finalizers were run without error (or there were none)
//      LUA_ERRxxx return code if any of the finalizers failed
//
// TBD: should we add stack trace on failing finalizer, wouldn't be hard..
//
static void push_stack_trace( lua_State* L, int rc_, int stk_base_);

static int run_finalizers( lua_State* L, int lua_rc)
{
    int finalizers_index;
    int n;
    int err_handler_index = 0;
    int rc = LUA_OK;                                                                // ...
    if( !push_registry_table( L, FINALIZER_REGKEY, FALSE))                          // ... finalizers?
    {
        return 0;   // no finalizers
    }

    STACK_GROW( L, 5);

    finalizers_index = lua_gettop( L);

#if ERROR_FULL_STACK
    lua_pushcfunction( L, lane_error);                                              // ... finalizers lane_error
    err_handler_index = lua_gettop( L);
#endif // ERROR_FULL_STACK

    for( n = (int) lua_rawlen( L, finalizers_index); n > 0; -- n)
    {
        int args = 0;
        lua_pushinteger( L, n);                                                       // ... finalizers lane_error n
        lua_rawget( L, finalizers_index);                                             // ... finalizers lane_error finalizer
        ASSERT_L( lua_isfunction( L, -1));
        if( lua_rc != LUA_OK) // we have an error message and an optional stack trace at the bottom of the stack
        {
            ASSERT_L( finalizers_index == 2 || finalizers_index == 3);
            //char const* err_msg = lua_tostring( L, 1);
            lua_pushvalue( L, 1);                                                       // ... finalizers lane_error finalizer err_msg
            // note we don't always have a stack trace for example when CANCEL_ERROR, or when we got an error that doesn't call our handler, such as LUA_ERRMEM
            if( finalizers_index == 3)
            {
                lua_pushvalue( L, 2);                                                     // ... finalizers lane_error finalizer err_msg stack_trace
            }
            args = finalizers_index - 1;
        }

        // if no error from the main body, finalizer doesn't receive any argument, else it gets the error message and optional stack trace
        rc = lua_pcall( L, args, 0, err_handler_index);                               // ... finalizers lane_error err_msg2?
        if( rc != LUA_OK)
        {
            push_stack_trace( L, rc, lua_gettop( L));
            // If one finalizer fails, don't run the others. Return this
            // as the 'real' error, replacing what we could have had (or not)
            // from the actual code.
            break;
        }
        // no error, proceed to next finalizer                                        // ... finalizers lane_error
    }

    if( rc != LUA_OK)
    {
        // ERROR_FULL_STACK accounts for the presence of lane_error on the stack
        int nb_err_slots = lua_gettop( L) - finalizers_index - ERROR_FULL_STACK;
        // a finalizer generated an error, this is what we leave of the stack
        for( n = nb_err_slots; n > 0; -- n)
        {
            lua_replace( L, n);
        }
        // leave on the stack only the error and optional stack trace produced by the error in the finalizer
        lua_settop( L, nb_err_slots);
    }
    else // no error from the finalizers, make sure only the original return values from the lane body remain on the stack
    {
        lua_settop( L, finalizers_index - 1);
    }

    return rc;
}

/*
 * ###############################################################################################
 * ########################################### Threads ###########################################
 * ###############################################################################################
 */

//
// Protects modifying the selfdestruct chain

#define SELFDESTRUCT_END ((Lane*)(-1))
//
// The chain is ended by '(Lane*)(-1)', not NULL:
//      'selfdestruct_first -> ... -> ... -> (-1)'

/*
 * Add the lane to selfdestruct chain; the ones still running at the end of the
 * whole process will be cancelled.
 */
static void selfdestruct_add( Lane* s)
{
    MUTEX_LOCK( &s->U->selfdestruct_cs);
    assert( s->selfdestruct_next == NULL);

    s->selfdestruct_next = s->U->selfdestruct_first;
    s->U->selfdestruct_first= s;
    MUTEX_UNLOCK( &s->U->selfdestruct_cs);
}

/*
 * A free-running lane has ended; remove it from selfdestruct chain
 */
static bool_t selfdestruct_remove( Lane* s)
{
    bool_t found = FALSE;
    MUTEX_LOCK( &s->U->selfdestruct_cs);
    {
        // Make sure (within the MUTEX) that we actually are in the chain
        // still (at process exit they will remove us from chain and then
        // cancel/kill).
        //
        if( s->selfdestruct_next != NULL)
        {
            Lane** ref = (Lane**) &s->U->selfdestruct_first;

            while( *ref != SELFDESTRUCT_END )
            {
                if( *ref == s)
                {
                    *ref = s->selfdestruct_next;
                    s->selfdestruct_next = NULL;
                    // the terminal shutdown should wait until the lane is done with its lua_close()
                    ++ s->U->selfdestructing_count;
                    found = TRUE;
                    break;
                }
                ref = (Lane**) &((*ref)->selfdestruct_next);
            }
            assert( found);
        }
    }
    MUTEX_UNLOCK( &s->U->selfdestruct_cs);
    return found;
}

/*
* Process end; cancel any still free-running threads
*/
static int selfdestruct_gc( lua_State* L)
{
    Universe* U = (Universe*) lua_touserdata( L, 1);

    while( U->selfdestruct_first != SELFDESTRUCT_END) // true at most once!
    {
        // Signal _all_ still running threads to exit (including the timer thread)
        //
        MUTEX_LOCK( &U->selfdestruct_cs);
        {
            Lane* s = U->selfdestruct_first;
            while( s != SELFDESTRUCT_END)
            {
                // attempt a regular unforced hard cancel with a small timeout
                bool_t cancelled = THREAD_ISNULL( s->thread) || thread_cancel( L, s, CO_Hard, 0.0001, FALSE, 0.0);
                // if we failed, and we know the thread is waiting on a linda
                if( cancelled == FALSE && s->status == WAITING && s->waiting_on != NULL)
                {
                    // signal the linda to wake up the thread so that it can react to the cancel query
                    // let us hope we never land here with a pointer on a linda that has been destroyed...
                    SIGNAL_T* waiting_on = s->waiting_on;
                    //s->waiting_on = NULL; // useful, or not?
                    SIGNAL_ALL( waiting_on);
                }
                s = s->selfdestruct_next;
            }
        }
        MUTEX_UNLOCK( &U->selfdestruct_cs);

        // When noticing their cancel, the lanes will remove themselves from
        // the selfdestruct chain.

        // TBD: Not sure if Windows (multi core) will require the timed approach,
        //      or single Yield. I don't have machine to test that (so leaving
        //      for timed approach).    -- AKa 25-Oct-2008

        // OS X 10.5 (Intel) needs more to avoid segfaults.
        //
        // "make test" is okay. 100's of "make require" are okay.
        //
        // Tested on MacBook Core Duo 2GHz and 10.5.5:
        //  -- AKa 25-Oct-2008
        //
        {
            lua_Number const shutdown_timeout = lua_tonumber( L, lua_upvalueindex( 1));
            double const t_until = now_secs() + shutdown_timeout;

            while( U->selfdestruct_first != SELFDESTRUCT_END)
            {
                YIELD();    // give threads time to act on their cancel
                {
                    // count the number of cancelled thread that didn't have the time to act yet
                    int n = 0;
                    double t_now = 0.0;
                    MUTEX_LOCK( &U->selfdestruct_cs);
                    {
                        Lane* s = U->selfdestruct_first;
                        while( s != SELFDESTRUCT_END)
                        {
                            if( s->cancel_request == CANCEL_HARD)
                                ++ n;
                            s = s->selfdestruct_next;
                        }
                    }
                    MUTEX_UNLOCK( &U->selfdestruct_cs);
                    // if timeout elapsed, or we know all threads have acted, stop waiting
                    t_now = now_secs();
                    if( n == 0 || (t_now >= t_until))
                    {
                        DEBUGSPEW_CODE( fprintf( stderr, "%d uncancelled lane(s) remain after waiting %fs at process end.\n", n, shutdown_timeout - (t_until - t_now)));
                        break;
                    }
                }
            }
        }

        // If some lanes are currently cleaning after themselves, wait until they are done.
        // They are no longer listed in the selfdestruct chain, but they still have to lua_close().
        while( U->selfdestructing_count > 0)
        {
            YIELD();
        }

        //---
        // Kill the still free running threads
        //
        if( U->selfdestruct_first != SELFDESTRUCT_END)
        {
            unsigned int n = 0;
            // first thing we did was to raise the linda signals the threads were waiting on (if any)
            // therefore, any well-behaved thread should be in CANCELLED state
            // these are not running, and the state can be closed
            MUTEX_LOCK( &U->selfdestruct_cs);
            {
                Lane* s = U->selfdestruct_first;
                while( s != SELFDESTRUCT_END)
                {
                    Lane* next_s = s->selfdestruct_next;
                    s->selfdestruct_next = NULL;     // detach from selfdestruct chain
                    if( !THREAD_ISNULL( s->thread)) // can be NULL if previous 'soft' termination succeeded
                    {
                        THREAD_KILL( &s->thread);
#if THREADAPI == THREADAPI_PTHREAD
                        // pthread: make sure the thread is really stopped!
                        THREAD_WAIT( &s->thread, -1, &s->done_signal, &s->done_lock, &s->status);
#endif // THREADAPI == THREADAPI_PTHREAD
                    }
                    // NO lua_close() in this case because we don't know where execution of the state was interrupted
                    lane_cleanup( s);
                    s = next_s;
                    ++ n;
                }
                U->selfdestruct_first = SELFDESTRUCT_END;
            }
            MUTEX_UNLOCK( &U->selfdestruct_cs);

            DEBUGSPEW_CODE( fprintf( stderr, "Killed %d lane(s) at process end.\n", n));
        }
    }

    // If some lanes are currently cleaning after themselves, wait until they are done.
    // They are no longer listed in the selfdestruct chain, but they still have to lua_close().
    while( U->selfdestructing_count > 0)
    {
        YIELD();
    }

    // necessary so that calling free_deep_prelude doesn't crash because linda_id expects a linda lightuserdata at absolute slot 1
    lua_settop( L, 0);
    // no need to mutex-protect this as all threads in the universe are gone at that point
    if( U->timer_deep != NULL) // test ins case some early internal error prevented Lanes from creating the deep timer
    {
        -- U->timer_deep->refcount; // should be 0 now
        free_deep_prelude( L, (DeepPrelude*) U->timer_deep);
        U->timer_deep = NULL;
    }

    close_keepers( U, L);

    // remove the protected allocator, if any
    cleanup_allocator_function( U, L);

#if HAVE_LANE_TRACKING()
    MUTEX_FREE( &U->tracking_cs);
#endif // HAVE_LANE_TRACKING()
    // Linked chains handling
    MUTEX_FREE( &U->selfdestruct_cs);
    MUTEX_FREE( &U->require_cs);
    // Locks for 'tools.c' inc/dec counters
    MUTEX_FREE( &U->deep_lock);
    MUTEX_FREE( &U->mtid_lock);
    // universe is no longer available (nor necessary)
    // we need to do this in case some deep userdata objects were created before Lanes was initialized,
    // as potentially they will be garbage collected after Lanes at application shutdown
    universe_store( L, NULL);
    return 0;
}


//---
// = _single( [cores_uint=1] )
//
// Limits the process to use only 'cores' CPU cores. To be used for performance
// testing on multicore devices. DEBUGGING ONLY!
//
LUAG_FUNC( set_singlethreaded)
{
    uint_t cores = luaG_optunsigned( L, 1, 1);
    (void) cores; // prevent "unused" warning

#ifdef PLATFORM_OSX
#ifdef _UTILBINDTHREADTOCPU
    if( cores > 1)
    {
        return luaL_error( L, "Limiting to N>1 cores not possible");
    }
    // requires 'chudInitialize()'
    utilBindThreadToCPU(0);     // # of CPU to run on (we cannot limit to 2..N CPUs?)
    return 0;
#else
    return luaL_error( L, "Not available: compile with _UTILBINDTHREADTOCPU");
#endif
#else
    return luaL_error( L, "not implemented");
#endif
}


/*
* str= lane_error( error_val|str )
*
* Called if there's an error in some lane; add call stack to error message 
* just like 'lua.c' normally does.
*
* ".. will be called with the error message and its return value will be the 
*     message returned on the stack by lua_pcall."
*
* Note: Rather than modifying the error message itself, it would be better
*     to provide the call stack (as string) completely separated. This would
*     work great with non-string error values as well (current system does not).
*     (This is NOT possible with the Lua 5.1 'lua_pcall()'; we could of course
*     implement a Lanes-specific 'pcall' of our own that does this). TBD!!! :)
*       --AKa 22-Jan-2009
*/
#if ERROR_FULL_STACK

// crc64/we of string "EXTENDED_STACKTRACE_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( EXTENDED_STACKTRACE_REGKEY, 0x2357c69a7c92c936); // used as registry key

LUAG_FUNC( set_error_reporting)
{
    bool_t equal;
    luaL_checktype( L, 1, LUA_TSTRING);
    lua_pushliteral( L, "extended");
    equal = lua_rawequal( L, -1, 1);
    lua_pop( L, 1);
    if( equal)
    {
        goto done;
    }
    lua_pushliteral( L, "basic");
    equal = !lua_rawequal( L, -1, 1);
    lua_pop( L, 1);
    if( equal)
    {
        return luaL_error( L, "unsupported error reporting model");
    }
done:
    REGISTRY_SET( L, EXTENDED_STACKTRACE_REGKEY, lua_pushboolean( L, equal));
    return 0;
}

static int lane_error( lua_State* L)
{
    lua_Debug ar;
    int n;
    bool_t extended;

    // error message (any type)
    STACK_CHECK_ABS( L, 1);                                                         // some_error

    // Don't do stack survey for cancelled lanes.
    //
    if( equal_unique_key( L, 1, CANCEL_ERROR))
    {
        return 1;   // just pass on
    }

    STACK_GROW( L, 3);
    REGISTRY_GET( L, EXTENDED_STACKTRACE_REGKEY);                                   // some_error basic|extended
    extended = lua_toboolean( L, -1);
    lua_pop( L, 1);                                                                 // some_error

    // Place stack trace at 'registry[lane_error]' for the 'lua_pcall()'
    // caller to fetch. This bypasses the Lua 5.1 limitation of only one
    // return value from error handler to 'lua_pcall()' caller.

    // It's adequate to push stack trace as a table. This gives the receiver
    // of the stack best means to format it to their liking. Also, it allows
    // us to add more stack info later, if needed.
    //
    // table of { "sourcefile.lua:<line>", ... }
    //
    lua_newtable( L);                                                                // some_error {}

    // Best to start from level 1, but in some cases it might be a C function
    // and we don't get '.currentline' for that. It's okay - just keep level
    // and table index growing separate.    --AKa 22-Jan-2009
    //
    for( n = 1; lua_getstack( L, n, &ar); ++ n)
    {
        lua_getinfo( L, extended ? "Sln" : "Sl", &ar);
        if( extended)
        {
            lua_newtable( L);                                                           // some_error {} {}

            lua_pushstring( L, ar.source);                                              // some_error {} {} source
            lua_setfield( L, -2, "source");                                             // some_error {} {}

            lua_pushinteger( L, ar.currentline);                                        // some_error {} {} currentline
            lua_setfield( L, -2, "currentline");                                        // some_error {} {}

            lua_pushstring( L, ar.name);                                                // some_error {} {} name
            lua_setfield( L, -2, "name");                                               // some_error {} {}

            lua_pushstring( L, ar.namewhat);                                            // some_error {} {} namewhat
            lua_setfield( L, -2, "namewhat");                                           // some_error {} {}

            lua_pushstring( L, ar.what);                                                // some_error {} {} what
            lua_setfield( L, -2, "what");                                               // some_error {} {}
        }
        else if( ar.currentline > 0)
        {
            lua_pushfstring( L, "%s:%d", ar.short_src, ar.currentline);                 // some_error {} "blah:blah"
        }
        else
        {
            lua_pushfstring( L, "%s:?", ar.short_src);                                  // some_error {} "blah"
        }
        lua_rawseti( L, -2, (lua_Integer) n);                                         // some_error {}
    }

    REGISTRY_SET( L, STACKTRACE_REGKEY, lua_insert( L, -2));                        // some_error

    STACK_END( L, 1);
    return 1;   // the untouched error value
}
#endif // ERROR_FULL_STACK

static void push_stack_trace( lua_State* L, int rc_, int stk_base_)
{
    // Lua 5.1 error handler is limited to one return value; it stored the stack trace in the registry
    switch( rc_)
    {
        case LUA_OK: // no error, body return values are on the stack
        break;

        case LUA_ERRRUN: // cancellation or a runtime error
#if ERROR_FULL_STACK // when ERROR_FULL_STACK, we installed a handler
        {
            STACK_CHECK( L, 0);
            // fetch the call stack table from the registry where the handler stored it
            STACK_GROW( L, 1);
            // yields nil if no stack was generated (in case of cancellation for example)
            REGISTRY_GET( L, STACKTRACE_REGKEY);                                       // err trace|nil
            STACK_END( L, 1);

            // For cancellation the error message is CANCEL_ERROR, and a stack trace isn't placed
            // For other errors, the message can be whatever was thrown, and we should have a stack trace table
            ASSERT_L( lua_type( L, 1 + stk_base_) == (equal_unique_key( L, stk_base_, CANCEL_ERROR) ? LUA_TNIL : LUA_TTABLE));
            // Just leaving the stack trace table on the stack is enough to get it through to the master.
            break;
        }
#endif // fall through if not ERROR_FULL_STACK

        case LUA_ERRMEM: // memory allocation error (handler not called)
        case LUA_ERRERR: // error while running the error handler (if any, for example an out-of-memory condition)
        default:
        // we should have a single value which is either a string (the error message) or CANCEL_ERROR
        ASSERT_L( (lua_gettop( L) == stk_base_) && ((lua_type( L, stk_base_) == LUA_TSTRING) || equal_unique_key( L, stk_base_, CANCEL_ERROR)));
        break;
    }
}

LUAG_FUNC( set_debug_threadname)
{
    DECLARE_CONST_UNIQUE_KEY( hidden_regkey, LG_set_debug_threadname);
    // C s_lane structure is a light userdata upvalue
    Lane* s = lua_touserdata( L, lua_upvalueindex( 1));
    luaL_checktype( L, -1, LUA_TSTRING);                           // "name"
    lua_settop( L, 1);
    STACK_CHECK_ABS( L, 1);
    // store a hidden reference in the registry to make sure the string is kept around even if a lane decides to manually change the "decoda_name" global...
    REGISTRY_SET( L, hidden_regkey, lua_pushvalue( L, -2));
    STACK_MID( L, 1);
    s->debug_name = lua_tostring( L, -1);
    // keep a direct pointer on the string
    THREAD_SETNAME( s->debug_name);
    // to see VM name in Decoda debugger Virtual Machine window
    lua_setglobal( L, "decoda_name");                              //
    STACK_END( L, 0);
    return 0;
}

LUAG_FUNC( get_debug_threadname)
{
    Lane* const s = lua_toLane( L, 1);
    luaL_argcheck( L, lua_gettop( L) == 1, 2, "too many arguments");
    lua_pushstring( L, s->debug_name);
    return 1;
}

LUAG_FUNC( set_thread_priority)
{
    int const prio = (int) luaL_checkinteger( L, 1);
    // public Lanes API accepts a generic range -3/+3
    // that will be remapped into the platform-specific scheduler priority scheme
    // On some platforms, -3 is equivalent to -2 and +3 to +2
    if( prio < THREAD_PRIO_MIN || prio > THREAD_PRIO_MAX)
    {
        return luaL_error( L, "priority out of range: %d..+%d (%d)", THREAD_PRIO_MIN, THREAD_PRIO_MAX, prio);
    }
    THREAD_SET_PRIORITY( prio);
    return 0;
}

LUAG_FUNC( set_thread_affinity)
{
    lua_Integer affinity = luaL_checkinteger( L, 1);
    if( affinity <= 0)
    {
        return luaL_error( L, "invalid affinity (%d)", affinity);
    }
    THREAD_SET_AFFINITY( (unsigned int) affinity);
    return 0;
}

#if USE_DEBUG_SPEW()
// can't use direct LUA_x errcode indexing because the sequence is not the same between Lua 5.1 and 5.2 :-(
// LUA_ERRERR doesn't have the same value
struct errcode_name
{
    int code;
    char const* name;
};

static struct errcode_name s_errcodes[] =
{
    { LUA_OK, "LUA_OK"},
    { LUA_YIELD, "LUA_YIELD"},
    { LUA_ERRRUN, "LUA_ERRRUN"},
    { LUA_ERRSYNTAX, "LUA_ERRSYNTAX"},
    { LUA_ERRMEM, "LUA_ERRMEM"},
    { LUA_ERRGCMM, "LUA_ERRGCMM"},
    { LUA_ERRERR, "LUA_ERRERR"},
};
static char const* get_errcode_name( int _code)
{
    int i;
    for( i = 0; i < 7; ++ i)
    {
        if( s_errcodes[i].code == _code)
        {
            return s_errcodes[i].name;
        }
    }
    return "<NULL>";
}
#endif // USE_DEBUG_SPEW()

#if THREADWAIT_METHOD == THREADWAIT_CONDVAR // implies THREADAPI == THREADAPI_PTHREAD
static void thread_cleanup_handler( void* opaque)
{
    Lane* s= (Lane*) opaque;
    MUTEX_LOCK( &s->done_lock);
    s->status = CANCELLED;
    SIGNAL_ONE( &s->done_signal);   // wake up master (while 's->done_lock' is on)
    MUTEX_UNLOCK( &s->done_lock);
}
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR

static THREAD_RETURN_T THREAD_CALLCONV lane_main( void* vs)
{
    Lane* s = (Lane*) vs;
    int rc, rc2;
    lua_State* L = s->L;
    // Called with the lane function and arguments on the stack
    int const nargs = lua_gettop( L) - 1;
    DEBUGSPEW_CODE( Universe* U = universe_get( L));
    THREAD_MAKE_ASYNCH_CANCELLABLE();
    THREAD_CLEANUP_PUSH( thread_cleanup_handler, s);
    s->status = RUNNING;  // PENDING -> RUNNING

    // Tie "set_finalizer()" to the state
    lua_pushcfunction( L, LG_set_finalizer);
    populate_func_lookup_table( L, -1, "set_finalizer");
    lua_setglobal( L, "set_finalizer");

    // Tie "set_debug_threadname()" to the state
    // But don't register it in the lookup database because of the s_lane pointer upvalue
    lua_pushlightuserdata( L, s);
    lua_pushcclosure( L, LG_set_debug_threadname, 1);
    lua_setglobal( L, "set_debug_threadname");

    // Tie "cancel_test()" to the state
    lua_pushcfunction( L, LG_cancel_test);
    populate_func_lookup_table( L, -1, "cancel_test");
    lua_setglobal( L, "cancel_test");

    // this could be done in lane_new before the lane body function is pushed on the stack to avoid unnecessary stack slot shifting around
#if ERROR_FULL_STACK
    // Tie "set_error_reporting()" to the state
    lua_pushcfunction( L, LG_set_error_reporting);
    populate_func_lookup_table( L, -1, "set_error_reporting");
    lua_setglobal( L, "set_error_reporting");

    STACK_GROW( L, 1);
    lua_pushcfunction( L, lane_error);                                           // func args handler
    lua_insert( L, 1);                                                           // handler func args
#endif // ERROR_FULL_STACK

    rc = lua_pcall( L, nargs, LUA_MULTRET, ERROR_FULL_STACK);                    // retvals|err

#if ERROR_FULL_STACK
    lua_remove( L, 1);                                                           // retvals|error
#	endif // ERROR_FULL_STACK

    // in case of error and if it exists, fetch stack trace from registry and push it
    push_stack_trace( L, rc, 1);                                                 // retvals|error [trace]

    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "Lane %p body: %s (%s)\n" INDENT_END, L, get_errcode_name( rc), equal_unique_key( L, 1, CANCEL_ERROR) ? "cancelled" : lua_typename( L, lua_type( L, 1))));
    //STACK_DUMP(L);
    // Call finalizers, if the script has set them up.
    //
    rc2 = run_finalizers( L, rc);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "Lane %p finalizer: %s\n" INDENT_END, L, get_errcode_name( rc2)));
    if( rc2 != LUA_OK) // Error within a finalizer!
    {
        // the finalizer generated an error, and left its own error message [and stack trace] on the stack
        rc = rc2;    // we're overruling the earlier script error or normal return
    }
    s->waiting_on = NULL; // just in case
    if( selfdestruct_remove( s)) // check and remove (under lock!)
    {
        // We're a free-running thread and no-one's there to clean us up.
        //
        lua_close( s->L);

        MUTEX_LOCK( &s->U->selfdestruct_cs);
        // done with lua_close(), terminal shutdown sequence may proceed
        -- s->U->selfdestructing_count;
        MUTEX_UNLOCK( &s->U->selfdestruct_cs);

        lane_cleanup( s); // s is freed at this point
    }
    else
    {
        // leave results (1..top) or error message + stack trace (1..2) on the stack - master will copy them

        enum e_status st = (rc == 0) ? DONE : equal_unique_key( L, 1, CANCEL_ERROR) ? CANCELLED : ERROR_ST;

        // Posix no PTHREAD_TIMEDJOIN:
        // 'done_lock' protects the -> DONE|ERROR_ST|CANCELLED state change
        //
#if THREADWAIT_METHOD == THREADWAIT_CONDVAR
        MUTEX_LOCK( &s->done_lock);
        {
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR
            s->status = st;
#if THREADWAIT_METHOD == THREADWAIT_CONDVAR
            SIGNAL_ONE( &s->done_signal);   // wake up master (while 's->done_lock' is on)
        }
        MUTEX_UNLOCK( &s->done_lock);
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR
    }
    THREAD_CLEANUP_POP( FALSE);
    return 0;   // ignored
}

// --- If a client wants to transfer stuff of a given module from the current state to another Lane, the module must be required
// with lanes.require, that will call the regular 'require', then populate the lookup database in the source lane
// module = lanes.require( "modname")
// upvalue[1]: _G.require
LUAG_FUNC( require)
{
    char const* name = lua_tostring( L, 1);
    int const nargs = lua_gettop( L);
    DEBUGSPEW_CODE( Universe* U = universe_get( L));
    STACK_CHECK( L, 0);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lanes.require %s BEGIN\n" INDENT_END, name));
    DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
    lua_pushvalue( L, lua_upvalueindex(1));   // "name" require
    lua_insert( L, 1);                        // require "name"
    lua_call( L, nargs, 1);                   // module
    populate_func_lookup_table( L, -1, name);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lanes.require %s END\n" INDENT_END, name));
    DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    STACK_END( L, 0);
    return 1;
}


// --- If a client wants to transfer stuff of a previously required module from the current state to another Lane, the module must be registered
// to populate the lookup database in the source lane (and in the destination too, of course)
// lanes.register( "modname", module)
LUAG_FUNC( register)
{
    char const* name = luaL_checkstring( L, 1);
    int const mod_type = lua_type( L, 2);
    // ignore extra parameters, just in case
    lua_settop( L, 2);
    luaL_argcheck( L, (mod_type == LUA_TTABLE) || (mod_type == LUA_TFUNCTION), 2, "unexpected module type");
    DEBUGSPEW_CODE( Universe* U = universe_get( L));
    STACK_CHECK( L, 0);                          // "name" mod_table
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lanes.register %s BEGIN\n" INDENT_END, name));
    DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
    populate_func_lookup_table( L, -1, name);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lanes.register %s END\n" INDENT_END, name));
    DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    STACK_END( L, 0);
    return 0;
}

// crc64/we of string "GCCB_KEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( GCCB_KEY, 0xcfb1f046ef074e88);

//---
// lane_ud = lane_new( function
//                   , [libs_str]
//                   , [priority_int=0]
//                   , [globals_tbl]
//                   , [package_tbl]
//                   , [required_tbl]
//                   , [gc_cb_func]
//                  [, ... args ...])
//
// Upvalues: metatable to use for 'lane_ud'
//
LUAG_FUNC( lane_new)
{
    lua_State* L2;
    Lane* s;
    Lane** ud;

    char const* libs_str = lua_tostring( L, 2);
    int const priority = (int) luaL_optinteger( L, 3, 0);
    uint_t globals_idx = lua_isnoneornil( L, 4) ? 0 : 4;
    uint_t package_idx = lua_isnoneornil( L, 5) ? 0 : 5;
    uint_t required_idx = lua_isnoneornil( L, 6) ? 0 : 6;
    uint_t gc_cb_idx = lua_isnoneornil( L, 7) ? 0 : 7;

#define FIXED_ARGS 7
    int const nargs = lua_gettop(L) - FIXED_ARGS;
    Universe* U = universe_get( L);
    ASSERT_L( nargs >= 0);

    // public Lanes API accepts a generic range -3/+3
    // that will be remapped into the platform-specific scheduler priority scheme
    // On some platforms, -3 is equivalent to -2 and +3 to +2
    if( priority < THREAD_PRIO_MIN || priority > THREAD_PRIO_MAX)
    {
        return luaL_error( L, "Priority out of range: %d..+%d (%d)", THREAD_PRIO_MIN, THREAD_PRIO_MAX, priority);
    }

    /* --- Create and prepare the sub state --- */
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: setup\n" INDENT_END));
    DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

    // populate with selected libraries at the same time
    L2 = luaG_newstate( U, L, libs_str);                     // L                                                                              // L2

    STACK_GROW( L2, nargs + 3);                                                                                                                //
    STACK_CHECK( L2, 0);

    STACK_GROW( L, 3);                                       // func libs priority globals package required gc_cb [... args ...]
    STACK_CHECK( L, 0);

    // give a default "Lua" name to the thread to see VM name in Decoda debugger
    lua_pushfstring( L2, "Lane #%p", L2);                                                                                                      // "..."
    lua_setglobal( L2, "decoda_name");                                                                                                         //
    ASSERT_L( lua_gettop( L2) == 0);

    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: update 'package'\n" INDENT_END));
    // package
    if( package_idx != 0)
    {
        // when copying with mode eLM_LaneBody, should raise an error in case of problem, not leave it one the stack
        (void) luaG_inter_copy_package( U, L, L2, package_idx, eLM_LaneBody);
    }

    // modules to require in the target lane *before* the function is transfered!

    if( required_idx != 0)
    {
        int nbRequired = 1;
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: require 'required' list\n" INDENT_END));
        DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
        // should not happen, was checked in lanes.lua before calling lane_new()
        if( lua_type( L, required_idx) != LUA_TTABLE)
        {
            return luaL_error( L, "expected required module list as a table, got %s", luaL_typename( L, required_idx));
        }

        lua_pushnil( L);                                       // func libs priority globals package required gc_cb [... args ...] nil
        while( lua_next( L, required_idx) != 0)                // func libs priority globals package required gc_cb [... args ...] n "modname"
        {
            if( lua_type( L, -1) != LUA_TSTRING || lua_type( L, -2) != LUA_TNUMBER || lua_tonumber( L, -2) != nbRequired)
            {
                return luaL_error( L, "required module list should be a list of strings");
            }
            else
            {
                // require the module in the target state, and populate the lookup table there too
                size_t len;
                char const* name = lua_tolstring( L, -1, &len);
                DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: require '%s'\n" INDENT_END, name));

                // require the module in the target lane
                lua_getglobal( L2, "require");                                                                                                       // require()?
                if( lua_isnil( L2, -1))
                {
                    lua_pop( L2, 1);                                                                                                                   //
                    luaL_error( L, "cannot pre-require modules without loading 'package' library first");
                }
                else
                {
                    lua_pushlstring( L2, name, len);                                                                                                   // require() name
                    if( lua_pcall( L2, 1, 1, 0) != LUA_OK)                                                                                             // ret/errcode
                    {
                        // propagate error to main state if any
                        luaG_inter_move( U, L2, L, 1, eLM_LaneBody);   // func libs priority globals package required gc_cb [... args ...] n "modname" error
                        return lua_error( L);
                    }
                    // after requiring the module, register the functions it exported in our name<->function database
                    populate_func_lookup_table( L2, -1, name);
                    lua_pop( L2, 1);                                                                                                                   //
                }
            }
            lua_pop( L, 1);                                      // func libs priority globals package required gc_cb [... args ...] n
            ++ nbRequired;
        }                                                      // func libs priority globals package required gc_cb [... args ...]
        DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    }
    STACK_MID( L, 0);
    STACK_MID( L2, 0);                                                                                                                         //

    // Appending the specified globals to the global environment
    // *after* stdlibs have been loaded and modules required, in case we transfer references to native functions they exposed...
    //
    if( globals_idx != 0)
    {
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: transfer globals\n" INDENT_END));
        if( !lua_istable( L, globals_idx))
        {
            return luaL_error( L, "Expected table, got %s", luaL_typename( L, globals_idx));
        }

        DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
        lua_pushnil( L);                                       // func libs priority globals package required gc_cb [... args ...] nil
        // Lua 5.2 wants us to push the globals table on the stack
        lua_pushglobaltable( L2);                                                                                                                // _G
        while( lua_next( L, globals_idx))                      // func libs priority globals package required gc_cb [... args ...] k v
        {
            luaG_inter_copy( U, L, L2, 2, eLM_LaneBody);                                                                                           // _G k v
            // assign it in L2's globals table
            lua_rawset( L2, -3);                                                                                                                   // _G
            lua_pop( L, 1);                                      // func libs priority globals package required gc_cb [... args ...] k
        }                                                      // func libs priority globals package required gc_cb [... args ...]
        lua_pop( L2, 1);                                                                                                                         //

        DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    }
    STACK_MID( L, 0);
    STACK_MID( L2, 0);

    // Lane main function
    if( lua_type( L, 1) == LUA_TFUNCTION)
    {
        int res;
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: transfer lane body\n" INDENT_END));
        DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
        lua_pushvalue( L, 1);                                  // func libs priority globals package required gc_cb [... args ...] func
        res = luaG_inter_move( U, L, L2, 1, eLM_LaneBody);     // func libs priority globals package required gc_cb [... args ...]    // func
        DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
        if( res != 0)
        {
            return luaL_error( L, "tried to copy unsupported types");
        }
    }
    else if( lua_type( L, 1) == LUA_TSTRING)
    {
        // compile the string
        if( luaL_loadstring( L2, lua_tostring( L, 1)) != 0)                                                                                      // func
        {
            return luaL_error( L, "error when parsing lane function code");
        }
    }
    STACK_MID( L, 0);
    STACK_MID( L2, 1);
    ASSERT_L( lua_isfunction( L2, 1));

    // revive arguments
    if( nargs > 0)
    {
        int res;
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: transfer lane arguments\n" INDENT_END));
        DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
        res = luaG_inter_move( U, L, L2, nargs, eLM_LaneBody); // func libs priority globals package required gc_cb                   // func [... args ...]
        DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
        if( res != 0)
        {
            return luaL_error( L, "tried to copy unsupported types");
        }
    }
    STACK_END( L, -nargs);
    ASSERT_L( lua_gettop( L) == FIXED_ARGS);
    STACK_CHECK( L, 0);
    STACK_MID( L2, 1 + nargs);

    // 's' is allocated from heap, not Lua, since its life span may surpass the handle's (if free running thread)
    //
    // a Lane full userdata needs a single uservalue
    ud = lua_newuserdatauv( L, sizeof( Lane*), 1);           // func libs priority globals package required gc_cb lane
    // don't hijack the state allocator when running LuaJIT because it looks like LuaJIT does not expect it and might invalidate the memory unexpectedly
#if USE_LUA_STATE_ALLOCATOR()
    {
        AllocatorDefinition* const allocD = &U->protected_allocator.definition;
        s = *ud = (Lane*)allocD->allocF(allocD->allocUD, NULL, 0, sizeof(Lane));
    }
#else // USE_LUA_STATE_ALLOCATOR()
    s = *ud = (Lane*) malloc(sizeof(Lane));
#endif // USE_LUA_STATE_ALLOCATOR()
    if( s == NULL)
    {
        return luaL_error( L, "could not create lane: out of memory");
    }

    s->L = L2;
    s->U = U;
    s->status = PENDING;
    s->waiting_on = NULL;
    s->debug_name = "<unnamed>";
    s->cancel_request = CANCEL_NONE;

#if THREADWAIT_METHOD == THREADWAIT_CONDVAR
    MUTEX_INIT( &s->done_lock);
    SIGNAL_INIT( &s->done_signal);
#endif // THREADWAIT_METHOD == THREADWAIT_CONDVAR
    s->mstatus = NORMAL;
    s->selfdestruct_next = NULL;
#if HAVE_LANE_TRACKING()
    s->tracking_next = NULL;
    if( s->U->tracking_first)
    {
        tracking_add( s);
    }
#endif // HAVE_LANE_TRACKING()

    // Set metatable for the userdata
    //
    lua_pushvalue( L, lua_upvalueindex( 1));                 // func libs priority globals package required gc_cb lane mt
    lua_setmetatable( L, -2);                                // func libs priority globals package required gc_cb lane
    STACK_MID( L, 1);

    // Create uservalue for the userdata
    // (this is where lane body return values will be stored when the handle is indexed by a numeric key)
    lua_newtable( L);                                        // func libs cancelstep priority globals package required gc_cb lane uv

    // Store the gc_cb callback in the uservalue
    if( gc_cb_idx > 0)
    {
        push_unique_key( L, GCCB_KEY);                         // func libs priority globals package required gc_cb lane uv k
        lua_pushvalue( L, gc_cb_idx);                          // func libs priority globals package required gc_cb lane uv k gc_cb
        lua_rawset( L, -3);                                    // func libs priority globals package required gc_cb lane uv
    }

    lua_setiuservalue( L, -2, 1);                            // func libs priority globals package required gc_cb lane

    // Store 's' in the lane's registry, for 'cancel_test()' (we do cancel tests at pending send/receive).
    REGISTRY_SET( L2, CANCEL_TEST_KEY, lua_pushlightuserdata( L2, s));                                                                         // func [... args ...]

    STACK_END( L, 1);
    STACK_END( L2, 1 + nargs);

    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "lane_new: launching thread\n" INDENT_END));
    THREAD_CREATE( &s->thread, lane_main, s, priority);

    DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    return 1;
}


//---
// = thread_gc( lane_ud )
//
// Cleanup for a thread userdata. If the thread is still executing, leave it
// alive as a free-running thread (will clean up itself).
//
// * Why NOT cancel/kill a loose thread: 
//
// At least timer system uses a free-running thread, they should be handy
// and the issue of canceling/killing threads at gc is not very nice, either
// (would easily cause waits at gc cycle, which we don't want).
//
LUAG_FUNC( thread_gc)
{
    bool_t have_gc_cb = FALSE;
    Lane* s = lua_toLane( L, 1);                                        // ud

    // if there a gc callback?
    lua_getiuservalue( L, 1, 1);                                        // ud uservalue
    push_unique_key( L, GCCB_KEY);                                      // ud uservalue __gc
    lua_rawget( L, -2);                                                 // ud uservalue gc_cb|nil
    if( !lua_isnil( L, -1))
    {
        lua_remove( L, -2);                                               // ud gc_cb|nil
        lua_pushstring( L, s->debug_name);                                // ud gc_cb name
        have_gc_cb = TRUE;
    }
    else
    {
        lua_pop( L, 2);                                                   // ud
    }

    // We can read 's->status' without locks, but not wait for it
    // test KILLED state first, as it doesn't need to enter the selfdestruct chain
    if( s->mstatus == KILLED)
    {
        // Make sure a kill has proceeded, before cleaning up the data structure.
        //
        // NO lua_close() in this case because we don't know where execution of the state was interrupted
        DEBUGSPEW_CODE( fprintf( stderr, "** Joining with a killed thread (needs testing) **"));
        // make sure the thread is no longer running, just like thread_join()
        if(! THREAD_ISNULL( s->thread))
        {
            THREAD_WAIT( &s->thread, -1, &s->done_signal, &s->done_lock, &s->status);
        }
        if( s->status >= DONE && s->L)
        {
            // we know the thread was killed while the Lua VM was not doing anything: we should be able to close it without crashing
            // now, thread_cancel() will not forcefully kill a lane with s->status >= DONE, so I am not sure it can ever happen
            lua_close( s->L);
            s->L = 0;
            // just in case, but s will be freed soon so...
            s->debug_name = "<gc>";
        }
        DEBUGSPEW_CODE( fprintf( stderr, "** Joined ok **"));
    }
    else if( s->status < DONE)
    {
        // still running: will have to be cleaned up later
        selfdestruct_add( s);
        assert( s->selfdestruct_next);
        if( have_gc_cb)
        {
            lua_pushliteral( L, "selfdestruct");                            // ud gc_cb name status
            lua_call( L, 2, 0);                                             // ud
        }
        return 0;
    }
    else if( s->L)
    {
        // no longer accessing the Lua VM: we can close right now
        lua_close( s->L);
        s->L = 0;
        // just in case, but s will be freed soon so...
        s->debug_name = "<gc>";
    }

    // Clean up after a (finished) thread
    lane_cleanup( s);

    // do this after lane cleanup in case the callback triggers an error
    if( have_gc_cb)
    {
        lua_pushliteral( L, "closed");                                    // ud gc_cb name status
        lua_call( L, 2, 0);                                               // ud
    }
    return 0;
}

//---
// str= thread_status( lane )
//
// Returns: "pending"   not started yet
//          -> "running"   started, doing its work..
//             <-> "waiting"   blocked in a receive()
//                -> "done"     finished, results are there
//                   / "error"     finished at an error, error value is there
//                   / "cancelled"   execution cancelled by M (state gone)
//
static char const * thread_status_string( Lane* s)
{
    enum e_status st = s->status;    // read just once (volatile)
    char const* str =
        (s->mstatus == KILLED) ? "killed" : // new to v3.3.0!
        (st == PENDING) ? "pending" :
        (st == RUNNING) ? "running" :    // like in 'co.status()'
        (st == WAITING) ? "waiting" :
        (st == DONE) ? "done" :
        (st == ERROR_ST) ? "error" :
        (st == CANCELLED) ? "cancelled" : NULL;
    return str;
}

int push_thread_status( lua_State* L, Lane* s)
{
    char const* const str = thread_status_string( s);
    ASSERT_L( str);

    lua_pushstring( L, str);
    return 1;
}


//---
// [...] | [nil, err_any, stack_tbl]= thread_join( lane_ud [, wait_secs=-1] )
//
//  timeout:   returns nil
//  done:      returns return values (0..N)
//  error:     returns nil + error value [+ stack table]
//  cancelled: returns nil
//
LUAG_FUNC( thread_join)
{
    Lane* const s = lua_toLane( L, 1);
    double wait_secs = luaL_optnumber( L, 2, -1.0);
    lua_State* L2 = s->L;
    int ret;
    bool_t done = THREAD_ISNULL( s->thread) || THREAD_WAIT( &s->thread, wait_secs, &s->done_signal, &s->done_lock, &s->status);
    if( !done || !L2)
    {
        STACK_GROW( L, 2);
        lua_pushnil( L);
        lua_pushliteral( L, "timeout");
        return 2;
    }

    STACK_CHECK( L, 0);
    // Thread is DONE/ERROR_ST/CANCELLED; all ours now

    if( s->mstatus == KILLED) // OS thread was killed if thread_cancel was forced
    {
        // in that case, even if the thread was killed while DONE/ERROR_ST/CANCELLED, ignore regular return values
        STACK_GROW( L, 2);
        lua_pushnil( L);
        lua_pushliteral( L, "killed");
        ret = 2;
    }
    else
    {
        Universe* U = universe_get( L);
        // debug_name is a pointer to string possibly interned in the lane's state, that no longer exists when the state is closed
        // so store it in the userdata uservalue at a key that can't possibly collide
        securize_debug_threadname( L, s);
        switch( s->status)
        {
            case DONE:
            {
                uint_t n = lua_gettop( L2);       // whole L2 stack
                if( (n > 0) && (luaG_inter_move( U, L2, L, n, eLM_LaneBody) != 0))
                {
                    return luaL_error( L, "tried to copy unsupported types");
                }
                ret = n;
            }
            break;

            case ERROR_ST:
            {
                int const n = lua_gettop( L2);
                STACK_GROW( L, 3);
                lua_pushnil( L);
                // even when ERROR_FULL_STACK, if the error is not LUA_ERRRUN, the handler wasn't called, and we only have 1 error message on the stack ...
                if( luaG_inter_move( U, L2, L, n, eLM_LaneBody) != 0)  // nil "err" [trace]
                {
                    return luaL_error( L, "tried to copy unsupported types: %s", lua_tostring( L, -n));
                }
                ret = 1 + n;
            }
            break;

            case CANCELLED:
            ret = 0;
            break;

            default:
            DEBUGSPEW_CODE( fprintf( stderr, "Status: %d\n", s->status));
            ASSERT_L( FALSE);
            ret = 0;
        }
        lua_close( L2);
    }
    s->L = 0;
    STACK_END( L, ret);
    return ret;
}


//---
// thread_index( ud, key) -> value
//
// If key is found in the environment, return it
// If key is numeric, wait until the thread returns and populate the environment with the return values
// If the return values signal an error, propagate it
// If key is "status" return the thread status
// Else raise an error
LUAG_FUNC( thread_index)
{
    int const UD = 1;
    int const KEY = 2;
    int const USR = 3;
    Lane* const s = lua_toLane( L, UD);
    ASSERT_L( lua_gettop( L) == 2);

    STACK_GROW( L, 8); // up to 8 positions are needed in case of error propagation

    // If key is numeric, wait until the thread returns and populate the environment with the return values
    if( lua_type( L, KEY) == LUA_TNUMBER)
    {
        // first, check that we don't already have an environment that holds the requested value
        {
            // If key is found in the uservalue, return it
            lua_getiuservalue( L, UD, 1);
            lua_pushvalue( L, KEY);
            lua_rawget( L, USR);
            if( !lua_isnil( L, -1))
            {
                return 1;
            }
            lua_pop( L, 1);
        }
        {
            // check if we already fetched the values from the thread or not
            bool_t fetched;
            lua_Integer key = lua_tointeger( L, KEY);
            lua_pushinteger( L, 0);
            lua_rawget( L, USR);
            fetched = !lua_isnil( L, -1);
            lua_pop( L, 1); // back to our 2 args + uservalue on the stack
            if( !fetched)
            {
                lua_pushinteger( L, 0);
                lua_pushboolean( L, 1);
                lua_rawset( L, USR);
                // wait until thread has completed
                lua_pushcfunction( L, LG_thread_join);
                lua_pushvalue( L, UD);
                lua_call( L, 1, LUA_MULTRET); // all return values are on the stack, at slots 4+
                switch( s->status)
                {
                    default:
                    if( s->mstatus != KILLED)
                    {
                        // this is an internal error, we probably never get here
                        lua_settop( L, 0);
                        lua_pushliteral( L, "Unexpected status: ");
                        lua_pushstring( L, thread_status_string( s));
                        lua_concat( L, 2);
                        lua_error( L);
                        break;
                    }
                    // fall through if we are killed, as we got nil, "killed" on the stack

                    case DONE: // got regular return values
                    {
                        int i, nvalues = lua_gettop( L) - 3;
                        for( i = nvalues; i > 0; -- i)
                        {
                            // pop the last element of the stack, to store it in the uservalue at its proper index
                            lua_rawseti( L, USR, i);
                        }
                    }
                    break;

                    case ERROR_ST: // got 3 values: nil, errstring, callstack table
                    // me[-2] could carry the stack table, but even 
                    // me[-1] is rather unnecessary (and undocumented);
                    // use ':join()' instead.   --AKa 22-Jan-2009
                    ASSERT_L( lua_isnil( L, 4) && !lua_isnil( L, 5) && lua_istable( L, 6));
                    // store errstring at key -1
                    lua_pushnumber( L, -1);
                    lua_pushvalue( L, 5);
                    lua_rawset( L, USR);
                    break;

                    case CANCELLED:
                     // do nothing
                    break;
                }
            }
            lua_settop( L, 3);                                                // UD KEY ENV
            if( key != -1)
            {
                lua_pushnumber( L, -1);                                         // UD KEY ENV -1
                lua_rawget( L, USR);                                            // UD KEY ENV "error"
                if( !lua_isnil( L, -1)) // an error was stored
                {
                    // Note: Lua 5.1 interpreter is not prepared to show
                    //       non-string errors, so we use 'tostring()' here
                    //       to get meaningful output.  --AKa 22-Jan-2009
                    //
                    //       Also, the stack dump we get is no good; it only
                    //       lists our internal Lanes functions. There seems
                    //       to be no way to switch it off, though.
                    //
                    // Level 3 should show the line where 'h[x]' was read
                    // but this only seems to work for string messages
                    // (Lua 5.1.4). No idea, why.   --AKa 22-Jan-2009
                    lua_getmetatable( L, UD);                                     // UD KEY ENV "error" mt
                    lua_getfield( L, -1, "cached_error");                         // UD KEY ENV "error" mt error()
                    lua_getfield( L, -2, "cached_tostring");                      // UD KEY ENV "error" mt error() tostring()
                    lua_pushvalue( L, 4);                                         // UD KEY ENV "error" mt error() tostring() "error"
                    lua_call( L, 1, 1); // tostring( errstring) -- just in case   // UD KEY ENV "error" mt error() "error"
                    lua_pushinteger( L, 3);                                       // UD KEY ENV "error" mt error() "error" 3
                    lua_call( L, 2, 0); // error( tostring( errstring), 3)        // UD KEY ENV "error" mt
                }
                else
                {
                    lua_pop( L, 1); // back to our 3 arguments on the stack
                }
            }
            lua_rawgeti( L, USR, (int)key);
        }
        return 1;
    }
    if( lua_type( L, KEY) == LUA_TSTRING)
    {
        char const * const keystr = lua_tostring( L, KEY);
        lua_settop( L, 2); // keep only our original arguments on the stack
        if( strcmp( keystr, "status") == 0)
        {
            return push_thread_status( L, s); // push the string representing the status
        }
        // return UD.metatable[key]
        lua_getmetatable( L, UD); // UD KEY mt
        lua_replace( L, -3);      // mt KEY
        lua_rawget( L, -2);       // mt value
        // only "cancel" and "join" are registered as functions, any other string will raise an error
        if( lua_iscfunction( L, -1))
        {
            return 1;
        }
        return luaL_error( L, "can't index a lane with '%s'", keystr);
    }
    // unknown key
    lua_getmetatable( L, UD);
    lua_getfield( L, -1, "cached_error");
    lua_pushliteral( L, "Unknown key: ");
    lua_pushvalue( L, KEY);
    lua_concat( L, 2);
    lua_call( L, 1, 0); // error( "Unknown key: " .. key) -> doesn't return
    return 0;
}

#if HAVE_LANE_TRACKING()
//---
// threads() -> {}|nil
//
// Return a list of all known lanes
LUAG_FUNC( threads)
{
  int const top = lua_gettop( L);
  Universe* U = universe_get( L);

  // List _all_ still running threads
  //
  MUTEX_LOCK( &U->tracking_cs);
  if( U->tracking_first && U->tracking_first != TRACKING_END)
  {
    Lane* s = U->tracking_first;
    int index = 0;
    lua_newtable( L);                                          // {}
    while( s != TRACKING_END)
    {
      // insert a { name, status } tuple, so that several lanes with the same name can't clobber each other
      lua_newtable( L);                                        // {} {}
      lua_pushstring( L, s->debug_name);                       // {} {} "name"
      lua_setfield( L, -2, "name");                            // {} {}
      push_thread_status( L, s);                               // {} {} "status"
      lua_setfield( L, -2, "status");                          // {} {}
      lua_rawseti( L, -2, ++ index);                           // {}
      s = s->tracking_next;
    }
  }
  MUTEX_UNLOCK( &U->tracking_cs);
  return lua_gettop( L) - top; // 0 or 1
}
#endif // HAVE_LANE_TRACKING()

/*
 * ###############################################################################################
 * ######################################## Timer support ########################################
 * ###############################################################################################
 */

/*
* secs= now_secs()
*
* Returns the current time, as seconds (millisecond resolution).
*/
LUAG_FUNC( now_secs )
{
    lua_pushnumber( L, now_secs() );
    return 1;
}

/*
* wakeup_at_secs= wakeup_conv( date_tbl )
*/
LUAG_FUNC( wakeup_conv )
{
    int year, month, day, hour, min, sec, isdst;
    struct tm t;
    memset( &t, 0, sizeof( t));
        //
        // .year (four digits)
        // .month (1..12)
        // .day (1..31)
        // .hour (0..23)
        // .min (0..59)
        // .sec (0..61)
        // .yday (day of the year)
        // .isdst (daylight saving on/off)

    STACK_CHECK( L, 0);
    lua_getfield( L, 1, "year" ); year= (int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield( L, 1, "month" ); month= (int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield( L, 1, "day" ); day= (int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield( L, 1, "hour" ); hour= (int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield( L, 1, "min" ); min= (int)lua_tointeger(L,-1); lua_pop(L,1);
    lua_getfield( L, 1, "sec" ); sec= (int)lua_tointeger(L,-1); lua_pop(L,1);

    // If Lua table has '.isdst' we trust that. If it does not, we'll let
    // 'mktime' decide on whether the time is within DST or not (value -1).
    //
    lua_getfield( L, 1, "isdst" );
    isdst= lua_isboolean(L,-1) ? lua_toboolean(L,-1) : -1;
    lua_pop(L,1);
    STACK_END( L, 0);

    t.tm_year= year-1900;
    t.tm_mon= month-1;     // 0..11
    t.tm_mday= day;        // 1..31
    t.tm_hour= hour;       // 0..23
    t.tm_min= min;         // 0..59
    t.tm_sec= sec;         // 0..60
    t.tm_isdst= isdst;     // 0/1/negative

    lua_pushnumber( L, (double) mktime( &t));   // ms=0
    return 1;
}

/*
 * ###############################################################################################
 * ######################################## Module linkage #######################################
 * ###############################################################################################
 */

extern int LG_linda( lua_State* L);
static const struct luaL_Reg lanes_functions [] = {
    {"linda", LG_linda},
    {"now_secs", LG_now_secs},
    {"wakeup_conv", LG_wakeup_conv},
    {"set_thread_priority", LG_set_thread_priority},
    {"set_thread_affinity", LG_set_thread_affinity},
    {"nameof", luaG_nameof},
    {"register", LG_register},
    {"set_singlethreaded", LG_set_singlethreaded},
    {NULL, NULL}
};

/*
 * One-time initializations
 * settings table it at position 1 on the stack
 * pushes an error string on the stack in case of problem
 */
static void init_once_LOCKED( void)
{
#if (defined PLATFORM_WIN32) || (defined PLATFORM_POCKETPC)
    now_secs();     // initialize 'now_secs()' internal offset
#endif

#if (defined PLATFORM_OSX) && (defined _UTILBINDTHREADTOCPU)
    chudInitialize();
#endif

    //---
    // Linux needs SCHED_RR to change thread priorities, and that is only
    // allowed for sudo'ers. SCHED_OTHER (default) has no priorities.
    // SCHED_OTHER threads are always lower priority than SCHED_RR.
    //
    // ^-- those apply to 2.6 kernel.  IF **wishful thinking** these 
    //     constraints will change in the future, non-sudo priorities can 
    //     be enabled also for Linux.
    //
#ifdef PLATFORM_LINUX
    sudo = (geteuid() == 0); // we are root?

    // If lower priorities (-2..-1) are wanted, we need to lift the main
    // thread to SCHED_RR and 50 (medium) level. Otherwise, we're always below 
    // the launched threads (even -2).
    //
#ifdef LINUX_SCHED_RR
    if( sudo)
    {
        struct sched_param sp;
        sp.sched_priority = _PRIO_0;
        PT_CALL( pthread_setschedparam( pthread_self(), SCHED_RR, &sp));
    }
#endif // LINUX_SCHED_RR
#endif // PLATFORM_LINUX
}

static volatile long s_initCount = 0;

// upvalue 1: module name
// upvalue 2: module table
// param 1: settings table
LUAG_FUNC( configure)
{
    Universe* U = universe_get( L);
    bool_t const from_master_state = (U == NULL);
    char const* name = luaL_checkstring( L, lua_upvalueindex( 1));
    _ASSERT_L( L, lua_type( L, 1) == LUA_TTABLE);

    /*
    ** Making one-time initializations.
    **
    ** When the host application is single-threaded (and all threading happens via Lanes)
    ** there is no problem. But if the host is multithreaded, we need to lock around the
    ** initializations.
    */
#if THREADAPI == THREADAPI_WINDOWS
    {
        static volatile int /*bool*/ go_ahead; // = 0
        if( InterlockedCompareExchange( &s_initCount, 1, 0) == 0)
        {
            init_once_LOCKED();
            go_ahead = 1; // let others pass
        }
        else
        {
            while( !go_ahead) { Sleep(1); } // changes threads
        }
    }
#else // THREADAPI == THREADAPI_PTHREAD
    if( s_initCount == 0)
    {
        static pthread_mutex_t my_lock = PTHREAD_MUTEX_INITIALIZER;
        pthread_mutex_lock( &my_lock);
        {
            // Recheck now that we're within the lock
            //
            if( s_initCount == 0)
            {
                init_once_LOCKED();
                s_initCount = 1;
            }
        }
        pthread_mutex_unlock( &my_lock);
    }
#endif // THREADAPI == THREADAPI_PTHREAD

    STACK_GROW( L, 4);
    STACK_CHECK_ABS( L, 1);                                                                // settings

    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%p: lanes.configure() BEGIN\n" INDENT_END, L));
    DEBUGSPEW_CODE( if( U) ++ U->debugspew_indent_depth);

    if( U == NULL)
    {
        U = universe_create( L);                                                           // settings universe
        DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
        lua_newtable( L);                                                                  // settings universe mt
        lua_getfield( L, 1, "shutdown_timeout");                                           // settings universe mt shutdown_timeout
        lua_pushcclosure( L, selfdestruct_gc, 1);                                          // settings universe mt selfdestruct_gc
        lua_setfield( L, -2, "__gc");                                                      // settings universe mt
        lua_setmetatable( L, -2);                                                          // settings universe
        lua_pop( L, 1);                                                                    // settings
        lua_getfield( L, 1, "verbose_errors");                                             // settings verbose_errors
        U->verboseErrors = lua_toboolean( L, -1);
        lua_pop( L, 1);                                                                    // settings
        lua_getfield( L, 1, "demote_full_userdata");                                       // settings demote_full_userdata
        U->demoteFullUserdata = lua_toboolean( L, -1);
        lua_pop( L, 1);                                                                    // settings
#if HAVE_LANE_TRACKING()
        MUTEX_INIT( &U->tracking_cs);
        lua_getfield( L, 1, "track_lanes");                                                // settings track_lanes
        U->tracking_first = lua_toboolean( L, -1) ? TRACKING_END : NULL;
        lua_pop( L, 1);                                                                    // settings
#endif // HAVE_LANE_TRACKING()
        // Linked chains handling
        MUTEX_INIT( &U->selfdestruct_cs);
        MUTEX_RECURSIVE_INIT( &U->require_cs);
        // Locks for 'tools.c' inc/dec counters
        MUTEX_INIT( &U->deep_lock);
        MUTEX_INIT( &U->mtid_lock);
        U->selfdestruct_first = SELFDESTRUCT_END;
        initialize_allocator_function( U, L);
        initialize_on_state_create( U, L);
        init_keepers( U, L);
        STACK_MID( L, 1);

        // Initialize 'timer_deep'; a common Linda object shared by all states
        lua_pushcfunction( L, LG_linda);                                                   // settings lanes.linda
        lua_pushliteral( L, "lanes-timer");                                                // settings lanes.linda "lanes-timer"
        lua_call( L, 1, 1);                                                                // settings linda
        STACK_MID( L, 2);

        // Proxy userdata contents is only a 'DEEP_PRELUDE*' pointer
        U->timer_deep = *(DeepPrelude**) lua_touserdata( L, -1);
        // increment refcount so that this linda remains alive as long as the universe exists.
        ++ U->timer_deep->refcount;
        lua_pop( L, 1);                                                                    // settings
    }
    STACK_MID( L, 1);

    // Serialize calls to 'require' from now on, also in the primary state
    serialize_require( DEBUGSPEW_PARAM_COMMA( U) L);

    // Retrieve main module interface table
    lua_pushvalue( L, lua_upvalueindex( 2));                                               // settings M
    // remove configure() (this function) from the module interface
    lua_pushnil( L);                                                                       // settings M nil
    lua_setfield( L, -2, "configure");                                                     // settings M
    // add functions to the module's table
    luaG_registerlibfuncs( L, lanes_functions);
#if HAVE_LANE_TRACKING()
    // register core.threads() only if settings say it should be available
    if( U->tracking_first != NULL)
    {
        lua_pushcfunction( L, LG_threads);                                                 // settings M LG_threads()
        lua_setfield( L, -2, "threads");                                                   // settings M
    }
#endif // HAVE_LANE_TRACKING()
    STACK_MID( L, 2);

    {
        char const* errmsg;
        errmsg = push_deep_proxy( U, L, (DeepPrelude*) U->timer_deep, 0, eLM_LaneBody);    // settings M timer_deep
        if( errmsg != NULL)
        {
            return luaL_error( L, errmsg);
        }
        lua_setfield( L, -2, "timer_gateway");                                             // settings M
    }
    STACK_MID( L, 2);

    // prepare the metatable for threads
    // contains keys: { __gc, __index, cached_error, cached_tostring, cancel, join, get_debug_threadname }
    //
    if( luaL_newmetatable( L, "Lane"))                                                     // settings M mt
    {
        lua_pushcfunction( L, LG_thread_gc);                                               // settings M mt LG_thread_gc
        lua_setfield( L, -2, "__gc");                                                      // settings M mt
        lua_pushcfunction( L, LG_thread_index);                                            // settings M mt LG_thread_index
        lua_setfield( L, -2, "__index");                                                   // settings M mt
        lua_getglobal( L, "error");                                                        // settings M mt error
        ASSERT_L( lua_isfunction( L, -1));
        lua_setfield( L, -2, "cached_error");                                              // settings M mt
        lua_getglobal( L, "tostring");                                                     // settings M mt tostring
        ASSERT_L( lua_isfunction( L, -1));
        lua_setfield( L, -2, "cached_tostring");                                           // settings M mt
        lua_pushcfunction( L, LG_thread_join);                                             // settings M mt LG_thread_join
        lua_setfield( L, -2, "join");                                                      // settings M mt
        lua_pushcfunction( L, LG_get_debug_threadname);                                    // settings M mt LG_get_debug_threadname
        lua_setfield( L, -2, "get_debug_threadname");                                      // settings M mt
        lua_pushcfunction( L, LG_thread_cancel);                                           // settings M mt LG_thread_cancel
        lua_setfield( L, -2, "cancel");                                                    // settings M mt
        lua_pushliteral( L, "Lane");                                                       // settings M mt "Lane"
        lua_setfield( L, -2, "__metatable");                                               // settings M mt
    }

    lua_pushcclosure( L, LG_lane_new, 1);                                                  // settings M lane_new
    lua_setfield( L, -2, "lane_new");                                                      // settings M

    // we can't register 'lanes.require' normally because we want to create an upvalued closure
    lua_getglobal( L, "require");                                                          // settings M require
    lua_pushcclosure( L, LG_require, 1);                                                   // settings M lanes.require
    lua_setfield( L, -2, "require");                                                       // settings M

    lua_pushfstring(
        L, "%d.%d.%d"
        , LANES_VERSION_MAJOR, LANES_VERSION_MINOR, LANES_VERSION_PATCH
    );                                                                                     // settings M VERSION
    lua_setfield( L, -2, "version");                                                       // settings M

    lua_pushinteger(L, THREAD_PRIO_MAX);                                                   // settings M THREAD_PRIO_MAX
    lua_setfield( L, -2, "max_prio");                                                      // settings M

    push_unique_key( L, CANCEL_ERROR);                                                     // settings M CANCEL_ERROR
    lua_setfield( L, -2, "cancel_error");                                                  // settings M

    STACK_MID( L, 2); // reference stack contains only the function argument 'settings'
    // we'll need this every time we transfer some C function from/to this state
    REGISTRY_SET( L, LOOKUP_REGKEY, lua_newtable( L));
    STACK_MID( L, 2);

    // register all native functions found in that module in the transferable functions database
    // we process it before _G because we don't want to find the module when scanning _G (this would generate longer names)
    // for example in package.loaded["lanes.core"].*
    populate_func_lookup_table( L, -1, name);
    STACK_MID( L, 2);

    // record all existing C/JIT-fast functions
    // Lua 5.2 no longer has LUA_GLOBALSINDEX: we must push globals table on the stack
    if( from_master_state)
    {
        // don't do this when called during the initialization of a new lane,
        // because we will do it after on_state_create() is called,
        // and we don't want to skip _G because of caching in case globals are created then
        lua_pushglobaltable( L);                                                           // settings M _G
        populate_func_lookup_table( L, -1, NULL);
        lua_pop( L, 1);                                                                    // settings M
    }
    lua_pop( L, 1);                                                                        // settings

    // set _R[CONFIG_REGKEY] = settings 
    REGISTRY_SET( L, CONFIG_REGKEY, lua_pushvalue( L, -2)); // -2 because CONFIG_REGKEY is pushed before the value itself
    STACK_END( L, 1);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%p: lanes.configure() END\n" INDENT_END, L));
    DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    // Return the settings table
    return 1;
}

#if defined PLATFORM_WIN32 && !defined NDEBUG
#include <signal.h>
#include <conio.h>

void signal_handler( int signal)
{
    if( signal == SIGABRT)
    {
        _cprintf( "caught abnormal termination!");
        abort();
    }
}

// helper to have correct callstacks when crashing a Win32 running on 64 bits Windows
// don't forget to toggle Debug/Exceptions/Win32 in visual Studio too!
static volatile long s_ecoc_initCount = 0;
static volatile int s_ecoc_go_ahead = 0;
static void EnableCrashingOnCrashes( void)
{
    if( InterlockedCompareExchange( &s_ecoc_initCount, 1, 0) == 0)
    {
        typedef BOOL (WINAPI* tGetPolicy)( LPDWORD lpFlags);
        typedef BOOL (WINAPI* tSetPolicy)( DWORD dwFlags);
        const DWORD EXCEPTION_SWALLOWING = 0x1;

        HMODULE kernel32 = LoadLibraryA("kernel32.dll");
        tGetPolicy pGetPolicy = (tGetPolicy)GetProcAddress(kernel32, "GetProcessUserModeExceptionPolicy");
        tSetPolicy pSetPolicy = (tSetPolicy)GetProcAddress(kernel32, "SetProcessUserModeExceptionPolicy");
        if( pGetPolicy && pSetPolicy)
        {
            DWORD dwFlags;
            if( pGetPolicy( &dwFlags))
            {
                // Turn off the filter
                pSetPolicy( dwFlags & ~EXCEPTION_SWALLOWING);
            }
        }
        //typedef void (* SignalHandlerPointer)( int);
        /*SignalHandlerPointer previousHandler =*/ signal( SIGABRT, signal_handler);

        s_ecoc_go_ahead = 1; // let others pass
    }
    else
    {
        while( !s_ecoc_go_ahead) { Sleep(1); } // changes threads
    }
}
#endif // PLATFORM_WIN32

int LANES_API luaopen_lanes_core( lua_State* L)
{
#if defined PLATFORM_WIN32 && !defined NDEBUG
    EnableCrashingOnCrashes();
#endif // defined PLATFORM_WIN32 && !defined NDEBUG

    STACK_GROW( L, 4);
    STACK_CHECK( L, 0);

    // Create main module interface table
    // we only have 1 closure, which must be called to configure Lanes
    lua_newtable( L);                                   // M
    lua_pushvalue( L, 1);                               // M "lanes.core"
    lua_pushvalue( L, -2);                              // M "lanes.core" M
    lua_pushcclosure( L, LG_configure, 2);              // M LG_configure()
    REGISTRY_GET( L, CONFIG_REGKEY);                    // M LG_configure() settings
    if( !lua_isnil( L, -1)) // this is not the first require "lanes.core": call configure() immediately
    {
        lua_pushvalue( L, -1);                            // M LG_configure() settings settings
        lua_setfield( L, -4, "settings");                 // M LG_configure() settings
        lua_call( L, 1, 0);                               // M
    }
    else
    {
        // will do nothing on first invocation, as we haven't stored settings in the registry yet
        lua_setfield( L, -3, "settings");                 // M LG_configure()
        lua_setfield( L, -2, "configure");                // M
    }

    STACK_END( L, 1);
    return 1;
}

static int default_luaopen_lanes( lua_State* L)
{
    int rc = luaL_loadfile( L, "lanes.lua") || lua_pcall( L, 0, 1, 0);
    if( rc != LUA_OK)
    {
        return luaL_error( L, "failed to initialize embedded Lanes");
    }
    return 1;
}

// call this instead of luaopen_lanes_core() when embedding Lua and Lanes in a custom application
void LANES_API luaopen_lanes_embedded( lua_State* L, lua_CFunction _luaopen_lanes)
{
    STACK_CHECK( L, 0);
    // pre-require lanes.core so that when lanes.lua calls require "lanes.core" it finds it is already loaded
    luaL_requiref( L, "lanes.core", luaopen_lanes_core, 0);                                       // ... lanes.core
    lua_pop( L, 1);                                                                               // ...
    STACK_MID( L, 0);
    // call user-provided function that runs the chunk "lanes.lua" from wherever they stored it
    luaL_requiref( L, "lanes", _luaopen_lanes ? _luaopen_lanes : default_luaopen_lanes, 0);       // ... lanes
    STACK_END( L, 1);
}
