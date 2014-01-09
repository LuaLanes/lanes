/*
 --
 -- KEEPER.C
 --
 -- Keeper state logic
 --
 -- This code is read in for each "keeper state", which are the hidden, inter-
 -- mediate data stores used by Lanes inter-state communication objects.
 --
 -- Author: Benoit Germain <bnt.germain@gmail.com>
 --
 -- C implementation replacement of the original keeper.lua
 --
 --[[
 ===============================================================================

 Copyright (C) 2011-2013 Benoit Germain <bnt.germain@gmail.com>

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

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "lua.h"
#include "lauxlib.h"

#include "threading.h"
#include "tools.h"
#include "keeper.h"

//###################################################################################
// Keeper implementation
//###################################################################################

#ifndef __min
#define __min( a, b) (((a) < (b)) ? (a) : (b))
#endif // __min

typedef struct
{
	int first;
	int count;
	int limit;
} keeper_fifo;

// replaces the fifo ud by its uservalue on the stack
static keeper_fifo* prepare_fifo_access( lua_State* L, int idx)
{
	keeper_fifo* fifo = (keeper_fifo*) lua_touserdata( L, idx);
	if( fifo != NULL)
	{
		idx = lua_absindex( L, idx);
		STACK_GROW( L, 1);
		// we can replace the fifo userdata in the stack without fear of it being GCed, there are other references around
		lua_getuservalue( L, idx);
		lua_replace( L, idx);
	}
	return fifo;
}

// in: nothing
// out: { first = 1, count = 0, limit = -1}
static void fifo_new( lua_State* L)
{
	keeper_fifo* fifo;
	STACK_GROW( L, 2);
	fifo = (keeper_fifo*) lua_newuserdata( L, sizeof( keeper_fifo));
	fifo->first = 1;
	fifo->count = 0;
	fifo->limit = -1;
	lua_newtable( L);
	lua_setuservalue( L, -2);
}

// in: expect fifo ... on top of the stack
// out: nothing, removes all pushed values from the stack
static void fifo_push( lua_State* L, keeper_fifo* fifo, int _count)
{
	int idx = lua_gettop( L) - _count;
	int start = fifo->first + fifo->count - 1;
	int i;
	// pop all additional arguments, storing them in the fifo
	for( i = _count; i >= 1; -- i)
	{
		// store in the fifo the value at the top of the stack at the specified index, popping it from the stack
		lua_rawseti( L, idx, start + i);
	}
	fifo->count += _count;
}

// in: fifo
// out: ...|nothing
// expects exactly 1 value on the stack!
// currently only called with a count of 1, but this may change in the future
// function assumes that there is enough data in the fifo to satisfy the request
static void fifo_peek( lua_State* L, keeper_fifo* fifo, int _count)
{
	int i;
	STACK_GROW( L, _count);
	for( i = 0; i < _count; ++ i)
	{
		lua_rawgeti( L, 1, fifo->first + i);
	}
}

// in: fifo
// out: remove the fifo from the stack, push as many items as required on the stack (function assumes they exist in sufficient number)
static void fifo_pop( lua_State* L, keeper_fifo* fifo, int _count)
{
	int fifo_idx = lua_gettop( L);           // ... fifo
	int i;
	// each iteration pushes a value on the stack!
	STACK_GROW( L, _count + 2);
	// skip first item, we will push it last
	for( i = 1; i < _count; ++ i)
	{
		int const at = fifo->first + i;
		// push item on the stack
		lua_rawgeti( L, fifo_idx, at);         // ... fifo val
		// remove item from the fifo
		lua_pushnil( L);                       // ... fifo val nil
		lua_rawseti( L, fifo_idx, at);         // ... fifo val
	}
	// now process first item
	{
		int const at = fifo->first;
		lua_rawgeti( L, fifo_idx, at);         // ... fifo vals val
		lua_pushnil( L);                       // ... fifo vals val nil
		lua_rawseti( L, fifo_idx, at);         // ... fifo vals val
		lua_replace( L, fifo_idx);             // ... vals
	}
	fifo->first += _count;
	fifo->count -= _count;
}

// in: linda_ud expected at *absolute* stack slot idx
// out: fifos[ud]
static void* const fifos_key = (void*) prepare_fifo_access;
static void push_table( lua_State* L, int idx)
{
	STACK_GROW( L, 4);
	STACK_CHECK( L);
	idx = lua_absindex( L, idx);
	lua_pushlightuserdata( L, fifos_key);        // ud fifos_key
	lua_rawget( L, LUA_REGISTRYINDEX);           // ud fifos
	lua_pushvalue( L, idx);                      // ud fifos ud
	lua_rawget( L, -2);                          // ud fifos fifos[ud]
	STACK_MID( L, 2);
	if( lua_isnil( L, -1))
	{
		lua_pop( L, 1);                            // ud fifos
		// add a new fifos table for this linda
		lua_newtable( L);                          // ud fifos fifos[ud]
		lua_pushvalue( L, idx);                    // ud fifos fifos[ud] ud
		lua_pushvalue( L, -2);                     // ud fifos fifos[ud] ud fifos[ud]
		lua_rawset( L, -4);                        // ud fifos fifos[ud]
	}
	lua_remove( L, -2);                          // ud fifos[ud]
	STACK_END( L, 1);
}

int keeper_push_linda_storage( lua_State* L, void* ptr)
{
	struct s_Keeper* K = keeper_acquire( ptr);
	lua_State* KL = K ? K->L : NULL;
	if( KL == NULL) return 0;
	STACK_CHECK( KL);
	lua_pushlightuserdata( KL, fifos_key);                      // fifos_key
	lua_rawget( KL, LUA_REGISTRYINDEX);                         // fifos
	lua_pushlightuserdata( KL, ptr);                            // fifos ud
	lua_rawget( KL, -2);                                        // fifos storage
	lua_remove( KL, -2);                                        // storage
	if( !lua_istable( KL, -1))
	{
		lua_pop( KL, 1);                                          //
		STACK_MID( KL, 0);
		return 0;
	}
	// move data from keeper to destination state                  KEEPER                       MAIN
	lua_pushnil( KL);                                           // storage nil
	STACK_CHECK( L);
	lua_newtable( L);                                                                        // out
	while( lua_next( KL, -2))                                   // storage key fifo
	{
		keeper_fifo* fifo = prepare_fifo_access( KL, -1);         // storage key fifo
		lua_pushvalue( KL, -2);                                   // storage key fifo key
		luaG_inter_move( KL, L, 1, eLM_FromKeeper);               // storage key fifo          // out key
		STACK_MID( L, 2);
		lua_newtable( L);                                                                      // out key keyout
		luaG_inter_move( KL, L, 1, eLM_FromKeeper);               // storage key               // out key keyout fifo
		lua_pushinteger( L, fifo->first);                                                      // out key keyout fifo first
		STACK_MID( L, 5);
		lua_setfield( L, -3, "first");                                                         // out key keyout fifo
		lua_pushinteger( L, fifo->count);                                                      // out key keyout fifo count
		STACK_MID( L, 5);
		lua_setfield( L, -3, "count");                                                         // out key keyout fifo
		lua_pushinteger( L, fifo->limit);                                                      // out key keyout fifo limit
		STACK_MID( L, 5);
		lua_setfield( L, -3, "limit");                                                         // out key keyout fifo
		lua_setfield( L, -2, "fifo");                                                          // out key keyout
		lua_rawset( L, -3);                                                                    // out
		STACK_MID( L, 1);
	}
	STACK_END( L, 1);
	lua_pop( KL, 1);                                            //
	STACK_END( KL, 0);
	keeper_release( K);
	return 1;
}

// in: linda_ud
int keepercall_clear( lua_State* L)
{
	STACK_GROW( L, 3);
	lua_pushlightuserdata( L, fifos_key);        // ud fifos_key
	lua_rawget( L, LUA_REGISTRYINDEX);           // ud fifos
	lua_pushvalue( L, 1);                        // ud fifos ud
	lua_pushnil( L);                             // ud fifos ud nil
	lua_rawset( L, -3);                          // ud fifos
	lua_pop( L, 1);                              // ud
	return 0;
}


// in: linda_ud, key, ...
// out: true|false
int keepercall_send( lua_State* L)
{
	keeper_fifo* fifo;
	int n = lua_gettop( L) - 2;
	push_table( L, 1);                           // ud key ... fifos
	// get the fifo associated to this key in this linda, create it if it doesn't exist
	lua_pushvalue( L, 2);                        // ud key ... fifos key
	lua_rawget( L, -2);                          // ud key ... fifos fifo
	if( lua_isnil( L, -1))
	{
		lua_pop( L, 1);                            // ud key ... fifos
		fifo_new( L);                              // ud key ... fifos fifo
		lua_pushvalue( L, 2);                      // ud key ... fifos fifo key
		lua_pushvalue( L, -2);                     // ud key ... fifos fifo key fifo
		lua_rawset( L, -4);                        // ud key ... fifos fifo
	}
	lua_remove( L, -2);                          // ud key ... fifo
	fifo = (keeper_fifo*) lua_touserdata( L, -1);
	if( fifo->limit >= 0 && fifo->count + n > fifo->limit)
	{
		lua_settop( L, 0);                         //
		lua_pushboolean( L, 0);                    // false
	}
	else
	{
		fifo = prepare_fifo_access( L, -1);
		lua_replace( L, 2);                        // ud fifo ...
		fifo_push( L, fifo, n);                    // ud fifo
		lua_settop( L, 0);                         //
		lua_pushboolean( L, 1);                    // true
	}
	return 1;
}

// in: linda_ud, key [, key]?
// out: (key, val) or nothing
int keepercall_receive( lua_State* L)
{
	int top = lua_gettop( L);
	int i;
	push_table( L, 1);                           // ud keys fifos
	lua_replace( L, 1);                          // fifos keys
	for( i = 2; i <= top; ++ i)
	{
		keeper_fifo* fifo;
		lua_pushvalue( L, i);                      // fifos keys key[i]
		lua_rawget( L, 1);                         // fifos keys fifo
		fifo = prepare_fifo_access( L, -1);        // fifos keys fifo
		if( fifo != NULL && fifo->count > 0)
		{
			fifo_pop( L, fifo, 1);                   // fifos keys val
			if( !lua_isnil( L, -1))
			{
				lua_replace( L, 1);                    // val keys
				lua_settop( L, i);                     // val keys key[i]
				if( i != 2)
				{
					lua_replace( L, 2);                  // val key keys
					lua_settop( L, 2);                   // val key
				}
				lua_insert( L, 1);                     // key, val
				return 2;
			}
		}
		lua_settop( L, top);                       // data keys
	}
	// nothing to receive
	return 0;
}

//in: linda_ud key mincount [maxcount]
int keepercall_receive_batched( lua_State* L)
{
	int const min_count = (int) lua_tointeger( L, 3);
	if( min_count > 0)
	{
		keeper_fifo* fifo;
		int const max_count = (int) luaL_optinteger( L, 4, min_count);
		lua_settop( L, 2);                                    // ud key
		lua_insert( L, 1);                                    // key ud
		push_table( L, 2);                                    // key ud fifos
		lua_remove( L, 2);                                    // key fifos
		lua_pushvalue( L, 1);                                 // key fifos key
		lua_rawget( L, 2);                                    // key fifos fifo
		lua_remove( L, 2);                                    // key fifo
		fifo = prepare_fifo_access( L, 2);                    // key fifo
		if( fifo != NULL && fifo->count >= min_count)
		{
			fifo_pop( L, fifo, __min( max_count, fifo->count)); // key ...
		}
		else
		{
			lua_settop( L, 0);
		}
		return lua_gettop( L);
	}
	else
	{
		return 0;
	}
}

// in: linda_ud key n
// out: true or nil
int keepercall_limit( lua_State* L)
{
	keeper_fifo* fifo;
	int limit = (int) lua_tointeger( L, 3);
	push_table( L, 1);                                 // ud key n fifos
	lua_replace( L, 1);                                // fifos key n
	lua_pop( L, 1);                                    // fifos key
	lua_pushvalue( L, -1);                             // fifos key key
	lua_rawget( L, -3);                                // fifos key fifo|nil
	fifo = (keeper_fifo*) lua_touserdata( L, -1);
	if( fifo ==  NULL)
	{                                                  // fifos key nil
		lua_pop( L, 1);                                  // fifos key
		fifo_new( L);                                    // fifos key fifo
		fifo = (keeper_fifo*) lua_touserdata( L, -1);
		lua_rawset( L, -3);                              // fifos
	}
	// remove any clutter on the stack
	lua_settop( L, 0);
	// return true if we decide that blocked threads waiting to write on that key should be awakened
	// this is the case if we detect the key was full but it is no longer the case
	if(
			 ((fifo->limit >= 0) && (fifo->count >= fifo->limit)) // the key was full if limited and count exceeded the previous limit
		&& ((limit < 0) || (fifo->count < limit)) // the key is not full if unlimited or count is lower than the new limit
	)
	{
		lua_pushboolean( L, 1);
	}
	// set the new limit
	fifo->limit = limit;
	// return 0 or 1 value
	return lua_gettop( L);
}

//in: linda_ud key [val]
//out: true or nil
int keepercall_set( lua_State* L)
{
	bool_t should_wake_writers = FALSE;
	STACK_GROW( L, 6);
	// make sure we have a value on the stack
	if( lua_gettop( L) == 2)                          // ud key val?
	{
		lua_pushnil( L);                                // ud key nil
	}

	// retrieve fifos associated with the linda
	push_table( L, 1);                                // ud key val fifos
	lua_replace( L, 1);                               // fifos key val

	if( !lua_isnil( L, 3)) // set/replace contents stored at the specified key?
	{
		keeper_fifo* fifo;
		lua_pushvalue( L, -2);                          // fifos key val key
		lua_rawget( L, 1);                              // fifos key val fifo|nil
		fifo = (keeper_fifo*) lua_touserdata( L, -1);
		if( fifo == NULL) // can be NULL if we store a value at a new key
		{                                               // fifos key val nil
			lua_pop( L, 1);                               // fifos key val
			fifo_new( L);                                 // fifos key val fifo
			lua_pushvalue( L, 2);                         // fifos key val fifo key
			lua_pushvalue( L, -2);                        // fifos key val fifo key fifo
			lua_rawset( L, 1);                            // fifos key val fifo
		}
		else // the fifo exists, we just want to update its contents
		{                                               // fifos key val fifo
			// we create room if the fifo was full but it is no longer the case
			should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit);
			// empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
			lua_newtable( L);                             // fifos key val fifo {}
			lua_setuservalue( L, -2);                     // fifos key val fifo
			fifo->first = 1;
			fifo->count = 0;
		}
		fifo = prepare_fifo_access( L, -1);
		lua_insert( L, -2);                             // fifos key fifo val
		fifo_push( L, fifo, 1);                         // fifos key fifo
	}
	else // val == nil: we clear the key contents
	{                                                 // fifos key nil
		keeper_fifo* fifo;
		lua_pop( L, 1);                                 // fifos key
		lua_pushvalue( L, -1);                          // fifos key key
		lua_rawget( L, 1);                              // fifos key fifo|nil
		// empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
		fifo = (keeper_fifo*) lua_touserdata( L, -1);
		if( fifo != NULL) // might be NULL if we set a nonexistent key to nil
		{                                               // fifos key fifo
			if( fifo->limit < 0) // fifo limit value is the default (unlimited): we can totally remove it
			{
				lua_pop( L, 1);                             // fifos key
				lua_pushnil( L);                            // fifos key nil
				lua_rawset( L, -3);                         // fifos
			}
			else
			{
				// we create room if the fifo was full but it is no longer the case
				should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit);
				lua_remove( L, -2);                         // fifos fifo
				lua_newtable( L);                           // fifos fifo {}
				lua_setuservalue( L, -2);                   // fifos fifo
				fifo->first = 1;
				fifo->count = 0;
			}
		}
	}
	return should_wake_writers ? (lua_pushboolean( L, 1), 1) : 0;
}

// in: linda_ud key
int keepercall_get( lua_State* L)
{
	keeper_fifo* fifo;
	push_table( L, 1);                                // ud key fifos
	lua_replace( L, 1);                               // fifos key
	lua_rawget( L, 1);                                // fifos fifo
	fifo = prepare_fifo_access( L, -1);               // fifos fifo
	if( fifo != NULL && fifo->count > 0)
	{
		lua_remove( L, 1);                              // fifo
		// read one value off the fifo
		fifo_peek( L, fifo, 1);                         // fifo ...
		return 1;
	}
	// no fifo was ever registered for this key, or it is empty
	return 0;
}

// in: linda_ud [, key [, ...]]
int keepercall_count( lua_State* L)
{
	int top;
	push_table( L, 1);                                   // ud keys fifos
	switch( lua_gettop( L))
	{
		// no key is specified: return a table giving the count of all known keys
		case 2:                                            // ud fifos
		lua_newtable( L);                                  // ud fifos out
		lua_replace( L, 1);                                // out fifos
		lua_pushnil( L);                                   // out fifos nil
		while( lua_next( L, 2))                            // out fifos key fifo
		{
			keeper_fifo* fifo = prepare_fifo_access( L, -1); // out fifos key fifo
			lua_pop( L, 1);                                  // out fifos key
			lua_pushvalue( L, -1);                           // out fifos key key
			lua_pushinteger( L, fifo->count);                // out fifos key key count
			lua_rawset( L, -5);                              // out fifos key
		}
		lua_pop( L, 1);                                    // out
		break;

		// 1 key is specified: return its count
		case 3:                                            // ud key fifos
		{
			keeper_fifo* fifo;
			lua_replace( L, 1);                              // fifos key
			lua_rawget( L, -2);                              // fifos fifo|nil
			if( lua_isnil( L, -1)) // the key is unknown
			{                                                // fifos nil
				lua_remove( L, -2);                            // nil
			}
			else // the key is known
			{                                                // fifos fifo
				fifo = prepare_fifo_access( L, -1);            // fifos fifo
				lua_pushinteger( L, fifo->count);              // fifos fifo count
				lua_replace( L, -3);                           // count fifo
				lua_pop( L, 1);                                // count
			}
		}
		break;

		// a variable number of keys is specified: return a table of their counts
		default:                                           // ud keys fifos
		lua_newtable( L);                                  // ud keys fifos out
		lua_replace( L, 1);                                // out keys fifos
		// shifts all keys up in the stack. potentially slow if there are a lot of them, but then it should be bearable
		lua_insert( L, 2);                                 // out fifos keys
		while( (top = lua_gettop( L)) > 2)
		{
			keeper_fifo* fifo;
			lua_pushvalue( L, -1);                           // out fifos keys key
			lua_rawget( L, 2);                               // out fifos keys fifo|nil
			fifo = prepare_fifo_access( L, -1);              // out fifos keys fifo|nil
			lua_pop( L, 1);                                  // out fifos keys
			if( fifo != NULL) // the key is known
			{
				lua_pushinteger( L, fifo->count);              // out fifos keys count
				lua_rawset( L, 1);                             // out fifos keys
			}
			else // the key is unknown
			{
				lua_pop( L, 1);                                // out fifos keys
			}
		}
		lua_pop( L, 1);                                    // out
	}
	ASSERT_L( lua_gettop( L) == 1);
	return 1;
}

//###################################################################################
// Keeper API, accessed from linda methods
//###################################################################################

/*---=== Keeper states ===---
*/

/*
* Pool of keeper states
*
* Access to keeper states is locked (only one OS thread at a time) so the 
* bigger the pool, the less chances of unnecessary waits. Lindas map to the
* keepers randomly, by a hash.
*/
static struct s_Keeper *GKeepers = NULL;
static int GNbKeepers = 0;

#if HAVE_KEEPER_ATEXIT_DESINIT
static void atexit_close_keepers( void)
#else // HAVE_KEEPER_ATEXIT_DESINIT
void close_keepers( void)
#endif // HAVE_KEEPER_ATEXIT_DESINIT
{
	int i;
	int const nbKeepers = GNbKeepers;
	// NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
	// when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
	// in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
	GNbKeepers = 0;
	for( i = 0; i < nbKeepers; ++ i)
	{
		lua_State* L = GKeepers[i].L;
		GKeepers[i].L = NULL;
		lua_close( L);
	}
	for( i = 0; i < nbKeepers; ++ i)
	{
		MUTEX_FREE( &GKeepers[i].lock_);
	}
	if( GKeepers != NULL)
	{
		free( GKeepers);
	}
	GKeepers = NULL;
}

/*
* Initialize keeper states
*
* If there is a problem, return an error message (NULL for okay).
*
* Note: Any problems would be design flaws; the created Lua state is left
*       unclosed, because it does not really matter. In production code, this
*       function never fails.
* settings table is at position 1 on the stack
*/
char const* init_keepers( lua_State* L)
{
	int i;
	PROPAGATE_ALLOCF_PREP( L);

	STACK_CHECK( L);
	lua_getfield( L, 1, "nb_keepers");
	GNbKeepers = (int) lua_tointeger( L, -1);
	lua_pop( L, 1);
	STACK_END( L, 0);
	assert( GNbKeepers >= 1);

	GKeepers = malloc( GNbKeepers * sizeof( struct s_Keeper));
	for( i = 0; i < GNbKeepers; ++ i)
	{
		lua_State* K = PROPAGATE_ALLOCF_ALLOC();
		if( K == NULL)
		{
			(void) luaL_error( L, "init_keepers() failed while creating keeper state; out of memory");
		}
		STACK_CHECK( K);

		// to see VM name in Decoda debugger
		lua_pushliteral( K, "Keeper #");
		lua_pushinteger( K, i + 1);
		lua_concat( K, 2);
		lua_setglobal( K, "decoda_name");

		// create the fifos table in the keeper state
		lua_pushlightuserdata( K, fifos_key);
		lua_newtable( K);
		lua_rawset( K, LUA_REGISTRYINDEX);

		STACK_END( K, 0);
		// we can trigger a GC from inside keeper_call(), where a keeper is acquired
		// from there, GC can collect a linda, which would acquire the keeper again, and deadlock the thread.
		MUTEX_RECURSIVE_INIT( &GKeepers[i].lock_);
		GKeepers[i].L = K;
	}
#if HAVE_KEEPER_ATEXIT_DESINIT
	atexit( atexit_close_keepers);
#endif // HAVE_KEEPER_ATEXIT_DESINIT
	return NULL; // ok
}

struct s_Keeper* keeper_acquire( void const* ptr)
{
	// can be 0 if this happens during main state shutdown (lanes is being GC'ed -> no keepers)
	if( GNbKeepers == 0)
	{
		return NULL;
	}
	else
	{
		/*
		* Any hashing will do that maps pointers to 0..GNbKeepers-1 
		* consistently.
		*
		* Pointers are often aligned by 8 or so - ignore the low order bits
		* have to cast to unsigned long to avoid compilation warnings about loss of data when converting pointer-to-integer
		*/
		unsigned int i = (unsigned int)(((unsigned long)(ptr) >> 3) % GNbKeepers);
		struct s_Keeper* K= &GKeepers[i];

		MUTEX_LOCK( &K->lock_);
		//++ K->count;
		return K;
	}
}

void keeper_release( struct s_Keeper* K)
{
	//-- K->count;
	if( K) MUTEX_UNLOCK( &K->lock_);
}

void keeper_toggle_nil_sentinels( lua_State* L, int _val_i, int _nil_to_sentinel)
{
	int i, n = lua_gettop( L);
	/* We could use an empty table in 'keeper.lua' as the sentinel, but maybe
	* checking for a lightuserdata is faster. (any unique value will do -> take the address of some global of ours)
	*/
	void* nil_sentinel = &GNbKeepers;
	for( i = _val_i; i <= n; ++ i)
	{
		if( _nil_to_sentinel)
		{
			if( lua_isnil( L, i))
			{
				lua_pushlightuserdata( L, nil_sentinel);
				lua_replace( L, i);
			}
		}
		else
		{
			if( lua_touserdata( L, i) == nil_sentinel)
			{
				lua_pushnil( L);
				lua_replace( L, i);
			}
		}
	}
}

/*
* Call a function ('func_name') in the keeper state, and pass on the returned
* values to 'L'.
*
* 'linda':          deep Linda pointer (used only as a unique table key, first parameter)
* 'starting_index': first of the rest of parameters (none if 0)
*
* Returns: number of return values (pushed to 'L') or -1 in case of error
*/
int keeper_call( lua_State* K, keeper_api_t func_, lua_State* L, void* linda, uint_t starting_index)
{
	int const args = starting_index ? (lua_gettop( L) - starting_index + 1) : 0;
	int const Ktos = lua_gettop( K);
	int retvals = -1;

	STACK_GROW( K, 2);

	PUSH_KEEPER_FUNC( K, func_);

	lua_pushlightuserdata( K, linda);

	if( (args == 0) || luaG_inter_copy( L, K, args, eLM_ToKeeper) == 0) // L->K
	{
		lua_call( K, 1 + args, LUA_MULTRET);

		retvals = lua_gettop( K) - Ktos;
		// note that this can raise a luaL_error while the keeper state (and its mutex) is acquired
		// this may interrupt a lane, causing the destruction of the underlying OS thread
		// after this, another lane making use of this keeper can get an error code from the mutex-locking function
		// when attempting to grab the mutex again (WINVER <= 0x400 does this, but locks just fine, I don't know about pthread)
		if( (retvals > 0) && luaG_inter_move( K, L, retvals, eLM_FromKeeper) != 0) // K->L
		{
			retvals = -1;
		}
	}
	// whatever happens, restore the stack to where it was at the origin
	lua_settop( K, Ktos);
	return retvals;
}
