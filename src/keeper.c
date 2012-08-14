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

static void atexit_close_keepers(void)
{
	int i;
	// 2-pass close, in case a keeper holds a reference to a linda bound to another keeoer
	for( i = 0; i < GNbKeepers; ++ i)
	{
		lua_State* L = GKeepers[i].L;
		GKeepers[i].L = 0;
		lua_close( L);
	}
	for( i = 0; i < GNbKeepers; ++ i)
	{
		MUTEX_FREE( &GKeepers[i].lock_);
	}
	if( GKeepers) free( GKeepers);
	GKeepers = NULL;
	GNbKeepers = 0;
}

/*
* Initialize keeper states
*
* If there is a problem, return an error message (NULL for okay).
*
* Note: Any problems would be design flaws; the created Lua state is left
*       unclosed, because it does not really matter. In production code, this
*       function never fails.
*/
char const* init_keepers( int const _nbKeepers, lua_CFunction _on_state_create)
{
	int i;
	assert( _nbKeepers >= 1);
	GNbKeepers = _nbKeepers;
	GKeepers = malloc( _nbKeepers * sizeof( struct s_Keeper));
	for( i = 0; i < _nbKeepers; ++ i)
	{

		// Initialize Keeper states with bare minimum of libs (those required by 'keeper.lua')
		// 
		// 'io' for debugging messages, 'package' because we need to require modules exporting idfuncs
		// the others because they export functions that we may store in a keeper for transfer between lanes
		lua_State* K = luaG_newstate( "*", _on_state_create);
		if (!K)
			return "out of memory";

		STACK_CHECK( K)
		// to see VM name in Decoda debugger
		lua_pushliteral( K, "Keeper #");
		lua_pushinteger( K, i + 1);
		lua_concat( K, 2);
		lua_setglobal( K, "decoda_name");

		// use package.loaders[2] to find keeper microcode
		lua_getglobal( K, "package");                  // package
		lua_getfield( K, -1, "loaders");               // package package.loaders
		lua_rawgeti( K, -1, 2);                        // package package.loaders package.loaders[2]
		lua_pushliteral( K, "lanes-keeper");           // package package.loaders package.loaders[2] "lanes-keeper"
		STACK_MID( K, 4);
		// first pcall loads lanes-keeper.lua, second one runs the chunk
		if( lua_pcall( K, 1 /*args*/, 1 /*results*/, 0 /*errfunc*/) || lua_pcall( K, 0 /*args*/, 0 /*results*/, 0 /*errfunc*/))
		{
			// LUA_ERRRUN / LUA_ERRMEM / LUA_ERRERR
			//
			char const* err = lua_tostring( K, -1);
			assert( err);
			return err;
		}                                              // package package.loaders
		STACK_MID( K, 2);
		lua_pop( K, 2);
		STACK_END( K, 0)
		MUTEX_INIT( &GKeepers[i].lock_);
		GKeepers[i].L = K;
		//GKeepers[i].count = 0;
	}
	// call close_keepers at the very last as we want to be sure no thread is GCing after.
	// (and therefore may perform linda object dereferencing after keepers are gone)
	atexit( atexit_close_keepers);
	return NULL;    // ok
}

// cause each keeper state to populate its database of transferable functions with those from the specified module
void populate_keepers( lua_State *L)
{
	size_t name_len;
	char const *name = luaL_checklstring( L, -1, &name_len);
	size_t package_path_len;
	char const *package_path;
	size_t package_cpath_len;
	char const *package_cpath;
	int i;

	// we need to make sure that package.path & package.cpath are the same in the keepers
// than what is currently in use when the module is required in the caller's Lua state
	STACK_CHECK(L)
	STACK_GROW( L, 3);
	lua_getglobal( L, "package");
	lua_getfield( L, -1, "path");
	package_path = luaL_checklstring( L, -1, &package_path_len);
	lua_getfield( L, -2, "cpath");
	package_cpath = luaL_checklstring( L, -1, &package_cpath_len);

	for( i = 0; i < GNbKeepers; ++ i)
	{
		lua_State *K = GKeepers[i].L;
		int res;
		MUTEX_LOCK( &GKeepers[i].lock_);
		STACK_CHECK(K)
		STACK_GROW( K, 2);
		lua_getglobal( K, "package");
		lua_pushlstring( K, package_path, package_path_len);
		lua_setfield( K, -2, "path");
		lua_pushlstring( K, package_cpath, package_cpath_len);
		lua_setfield( K, -2, "cpath");
		lua_pop( K, 1);
		lua_getglobal( K, "require");
		lua_pushlstring( K, name, name_len);
		res = lua_pcall( K, 1, 0, 0);
		if( res != 0)
		{
			char const *err = luaL_checkstring( K, -1);
			luaL_error( L, "error requiring '%s' in keeper state: %s", name, err);
		}
		STACK_END(K, 0)
		MUTEX_UNLOCK( &GKeepers[i].lock_);
	}
	lua_pop( L, 3);
	STACK_END(L, 0)
}

struct s_Keeper *keeper_acquire( const void *ptr)
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
		*/
		unsigned int i= ((unsigned long)(ptr) >> 3) % GNbKeepers;
		struct s_Keeper *K= &GKeepers[i];

		MUTEX_LOCK( &K->lock_);
		//++ K->count;
		return K;
	}
}

void keeper_release( struct s_Keeper *K)
{
	//-- K->count;
	if( K) MUTEX_UNLOCK( &K->lock_);
}

void keeper_toggle_nil_sentinels( lua_State *L, int _val_i, int _nil_to_sentinel)
{
	int i, n = lua_gettop( L);
	/* We could use an empty table in 'keeper.lua' as the sentinel, but maybe
	* checking for a lightuserdata is faster. (any unique value will do -> take the address of some global of ours)
	*/
	void *nil_sentinel = &GNbKeepers;
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
