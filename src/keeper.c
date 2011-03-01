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

 Copyright (C) 2011 Benoit Germain <bnt.germain@gmail.com>

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

/*
* Lua code for the keeper states (baked in)
*/
static char const keeper_chunk[]= 
#include "keeper.lch"

/*
* Initialize keeper states
*
* If there is a problem, return an error message (NULL for okay).
*
* Note: Any problems would be design flaws; the created Lua state is left
*       unclosed, because it does not really matter. In production code, this
*       function never fails.
*/
char const *init_keepers( int const _nbKeepers)
{
	int i;
	assert( _nbKeepers >= 1);
	GNbKeepers = _nbKeepers;
	GKeepers = malloc( _nbKeepers * sizeof( struct s_Keeper));
	for( i = 0; i < _nbKeepers; ++ i)
	{

		// Initialize Keeper states with bare minimum of libs (those required
		// by 'keeper.lua')
		//
		lua_State *L= luaL_newstate();
		if (!L)
			return "out of memory";

		// to see VM name in Decoda debugger
		lua_pushliteral( L, "Keeper #");
		lua_pushinteger( L, i + 1);
		lua_concat( L, 2);
		lua_setglobal( L, "decoda_name");

		luaG_openlibs( L, "io,table,package" );     // 'io' for debugging messages, package because we need to require modules exporting idfuncs
		serialize_require( L);

		/* We could use an empty table in 'keeper.lua' as the sentinel, but maybe
		* checking for a lightuserdata is faster. (any unique value will do -> take the address of some global of ours)
		*/
		lua_pushlightuserdata( L, &GNbKeepers);
		lua_setglobal( L, "nil_sentinel");

		// Read in the preloaded chunk (and run it)
		//
		if (luaL_loadbuffer( L, keeper_chunk, sizeof(keeper_chunk), "=lanes_keeper" ))
			return "luaL_loadbuffer() failed";   // LUA_ERRMEM

		if (lua_pcall( L, 0 /*args*/, 0 /*results*/, 0 /*errfunc*/ ))
		{
			// LUA_ERRRUN / LUA_ERRMEM / LUA_ERRERR
			//
			const char *err= lua_tostring(L,-1);
			assert(err);
			return err;
		}

		MUTEX_INIT( &GKeepers[i].lock_ );
		GKeepers[i].L= L;
		//GKeepers[i].count = 0;
	}
	return NULL;    // ok
}

struct s_Keeper *keeper_acquire( const void *ptr)
{
	/*
	* Any hashing will do that maps pointers to 0..GNbKeepers-1 
	* consistently.
	*
	* Pointers are often aligned by 8 or so - ignore the low order bits
	*/
	unsigned int i= ((unsigned long)(ptr) >> 3) % GNbKeepers;
	struct s_Keeper *K= &GKeepers[i];

	MUTEX_LOCK( &K->lock_);
	//++ K->count;
	return K;
}

void keeper_release( struct s_Keeper *K)
{
	//-- K->count;
	MUTEX_UNLOCK( &K->lock_);
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
int keeper_call( lua_State *K, char const *func_name, lua_State *L, void *linda, uint_t starting_index)
{
	int const args = starting_index ? (lua_gettop(L) - starting_index +1) : 0;
	int const Ktos = lua_gettop(K);
	int retvals = -1;

	STACK_GROW( K, 2);

	lua_getglobal( K, func_name);
	ASSERT_L( lua_isfunction(K, -1));

	lua_pushlightuserdata( K, linda);

	if( (args == 0) || luaG_inter_copy( L, K, args) == 0) // L->K
	{
		lua_call( K, 1 + args, LUA_MULTRET);

		retvals = lua_gettop( K) - Ktos;
		if( (retvals > 0) && luaG_inter_move( K, L, retvals) != 0) // K->L
		{
			retvals = -1;
		}
	}
	// whatever happens, restore the stack to where it was at the origin
	lua_settop( K, Ktos);
	return retvals;
}

void close_keepers(void)
{
	int i;
	for( i = 0; i < GNbKeepers; ++ i)
	{
		lua_close( GKeepers[i].L);
		GKeepers[i].L = 0;
		//assert( GKeepers[i].count == 0);
	}
	if( GKeepers) free( GKeepers);
	GKeepers = NULL;
	GNbKeepers = 0;
}
