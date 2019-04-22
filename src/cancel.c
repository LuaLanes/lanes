/*
--
-- CANCEL.C
--
-- Lane cancellation support
--
-- Author: Benoit Germain <bnt.germain@gmail.com>
--
--[[
===============================================================================

Copyright (C) 2011-2019 Benoit Germain <bnt.germain@gmail.com>

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
]]--
*/

#include <assert.h>

#include "threading.h"
#include "cancel.h"
#include "tools.h"
#include "lanes_private.h"

// ################################################################################################
// ################################################################################################

/*
* Check if the thread in question ('L') has been signalled for cancel.
*
* Called by cancellation hooks and/or pending Linda operations (because then
* the check won't affect performance).
*
* Returns TRUE if any locks are to be exited, and 'cancel_error()' called,
* to make execution of the lane end.
*/
static inline enum e_cancel_request cancel_test( lua_State* L)
{
	Lane* const s = get_lane_from_registry( L);
	// 's' is NULL for the original main state (and no-one can cancel that)
	return s ? s->cancel_request : CANCEL_NONE;
}

// ################################################################################################

//---
// bool = cancel_test()
//
// Available inside the global namespace of lanes
// returns a boolean saying if a cancel request is pending
//
LUAG_FUNC( cancel_test)
{
	enum e_cancel_request test = cancel_test( L);
	lua_pushboolean( L, test != CANCEL_NONE);
	return 1;
}

// ################################################################################################
// ################################################################################################

void cancel_hook( lua_State* L, lua_Debug* ar)
{
	(void)ar;
	DEBUGSPEW_CODE( fprintf( stderr, "cancel_hook\n"));
	if( cancel_test( L) != CANCEL_NONE)
	{
		cancel_error( L);
	}
}

// ################################################################################################
// ################################################################################################

//---
// = thread_cancel( lane_ud [,timeout_secs=0.0] [,force_kill_bool=false] )
//
// The originator thread asking us specifically to cancel the other thread.
//
// 'timeout': <0: wait forever, until the lane is finished
//            0.0: just signal it to cancel, no time waited
//            >0: time to wait for the lane to detect cancellation
//
// 'force_kill': if true, and lane does not detect cancellation within timeout,
//            it is forcefully killed. Using this with 0.0 timeout means just kill
//            (unless the lane is already finished).
//
// Returns: true if the lane was already finished (DONE/ERROR_ST/CANCELLED) or if we
//          managed to cancel it.
//          false if the cancellation timed out, or a kill was needed.
//

// ################################################################################################

static cancel_result thread_cancel_soft( Lane* s, bool_t wake_lindas_)
{
	s->cancel_request = CANCEL_SOFT;    // it's now signaled to stop
																			// negative timeout: we don't want to truly abort the lane, we just want it to react to cancel_test() on its own
	if( wake_lindas_) // wake the thread so that execution returns from any pending linda operation if desired
	{
		SIGNAL_T *waiting_on = s->waiting_on;
		if( s->status == WAITING && waiting_on != NULL)
		{
			SIGNAL_ALL( waiting_on);
		}
	}
	// say we succeeded though
	return CR_Cancelled;
}

// ################################################################################################

static cancel_result thread_cancel_hard( lua_State* L, Lane* s, double secs_, bool_t force_, double waitkill_timeout_)
{
	cancel_result result;

	s->cancel_request = CANCEL_HARD;    // it's now signaled to stop
	{
		SIGNAL_T *waiting_on = s->waiting_on;
		if( s->status == WAITING && waiting_on != NULL)
		{
			SIGNAL_ALL( waiting_on);
		}
	}

	result = THREAD_WAIT( &s->thread, secs_, &s->done_signal, &s->done_lock, &s->status) ? CR_Cancelled : CR_Timeout;

	if( (result == CR_Timeout) && force_)
	{
		// Killing is asynchronous; we _will_ wait for it to be done at
		// GC, to make sure the data structure can be released (alternative
		// would be use of "cancellation cleanup handlers" that at least
		// PThread seems to have).
		//
		THREAD_KILL( &s->thread);
#if THREADAPI == THREADAPI_PTHREAD
		// pthread: make sure the thread is really stopped!
		// note that this may block forever if the lane doesn't call a cancellation point and pthread doesn't honor PTHREAD_CANCEL_ASYNCHRONOUS
		result = THREAD_WAIT( &s->thread, waitkill_timeout_, &s->done_signal, &s->done_lock, &s->status);
		if( result == CR_Timeout)
		{
			return luaL_error( L, "force-killed lane failed to terminate within %f second%s", waitkill_timeout_, waitkill_timeout_ > 1 ? "s" : "");
		}
#else
		(void) waitkill_timeout_; // unused
		(void) L; // unused
#endif // THREADAPI == THREADAPI_PTHREAD
		s->mstatus = KILLED;     // mark 'gc' to wait for it
														 // note that s->status value must remain to whatever it was at the time of the kill
														 // because we need to know if we can lua_close() the Lua State or not.
		result = CR_Killed;
	}
	return result;
}

// ################################################################################################

cancel_result thread_cancel( lua_State* L, Lane* s, double secs_, bool_t force_, double waitkill_timeout_)
{
	// remember that lanes are not transferable: only one thread can cancel a lane, so no multithreading issue here
	// We can read 's->status' without locks, but not wait for it (if Posix no PTHREAD_TIMEDJOIN)
	if( s->mstatus == KILLED)
	{
		return CR_Killed;
	}

	if( s->status >= DONE)
	{
		// say "ok" by default, including when lane is already done
		return CR_Cancelled;
	}

	// signal the linda the wake up the thread so that it can react to the cancel query
	// let us hope we never land here with a pointer on a linda that has been destroyed...
	if( secs_ < 0.0)
	{
		return thread_cancel_soft( s, force_);
	}

	return thread_cancel_hard( L, s, secs_, force_, waitkill_timeout_);
}

// ################################################################################################
// ################################################################################################

// lane_h:cancel( [timeout] [, force [, forcekill_timeout]])
LUAG_FUNC( thread_cancel)
{
	Lane* s = lua_toLane( L, 1);
	double secs = 0.0;
	int force_i = 2;
	int forcekill_timeout_i = 3;

	if( lua_isnumber( L, 2))
	{
		secs = lua_tonumber( L, 2);
		if( secs < 0.0 && lua_gettop( L) > 3)
		{
			return luaL_error( L, "can't force_kill a soft cancel");
		}
		// negative timeout and force flag means we want to wake linda-waiting threads
		++ force_i;
		++ forcekill_timeout_i;
	}
	else if( lua_isnil( L, 2))
	{
		++ force_i;
		++ forcekill_timeout_i;
	}

	{
		bool_t force = lua_toboolean( L, force_i);     // FALSE if nothing there
		double forcekill_timeout = luaL_optnumber( L, forcekill_timeout_i, 0.0);

		switch( thread_cancel( L, s, secs, force, forcekill_timeout))
		{
		case CR_Timeout:
			lua_pushboolean( L, 0);
			lua_pushstring( L, "timeout");
			return 2;

		case CR_Cancelled:
			lua_pushboolean( L, 1);
			return 1;

		case CR_Killed:
			lua_pushboolean( L, 0);
			lua_pushstring( L, "killed");
			return 2;
		}
	}
	// should never happen, only here to prevent the compiler from complaining of "not all control paths returning a value"
	return 0;
}
