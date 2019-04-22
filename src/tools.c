/*
 * TOOLS.C   	                    Copyright (c) 2002-10, Asko Kauppi
 *
 * Lua tools to support Lanes.
*/

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
              2011-17 benoit Germain <bnt.germain@gmail.com>

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

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#if !defined(__APPLE__)
#include <malloc.h>
#endif // __APPLE__

#include "tools.h"
#include "compat.h"
#include "universe.h"
#include "keeper.h"
#include "lanes.h"
#include "uniquekey.h"

// functions implemented in deep.c
extern bool_t copydeep( Universe* U, lua_State* L, lua_State* L2, int index, LookupMode mode_);
extern void push_registry_subtable( lua_State* L, UniqueKey key_);

DEBUGSPEW_CODE( char const* debugspew_indent = "----+----!----+----!----+----!----+----!----+----!----+----!----+----!----+");


/*---=== luaG_dump ===---*/
#ifdef _DEBUG
void luaG_dump( lua_State* L)
{
	int top = lua_gettop( L);
	int i;

	fprintf( stderr, "\n\tDEBUG STACK:\n");

	if( top == 0)
		fprintf( stderr, "\t(none)\n");

	for( i = 1; i <= top; ++ i)
	{
		int type = lua_type( L, i);

		fprintf( stderr, "\t[%d]= (%s) ", i, lua_typename( L, type));

		// Print item contents here...
		//
		// Note: this requires 'tostring()' to be defined. If it is NOT,
		//       enable it for more debugging.
		//
		STACK_CHECK( L, 0);
		STACK_GROW( L, 2);

		lua_getglobal( L, "tostring");
		//
		// [-1]: tostring function, or nil

		if( !lua_isfunction( L, -1))
		{
			fprintf( stderr, "('tostring' not available)");
		}
		else
		{
			lua_pushvalue( L, i);
			lua_call( L, 1 /*args*/, 1 /*retvals*/);

			// Don't trust the string contents
			//                
			fprintf( stderr, "%s", lua_tostring( L, -1));
		}
		lua_pop( L, 1);
		STACK_END( L, 0);
		fprintf( stderr, "\n");
	}
	fprintf( stderr, "\n");
}
#endif // _DEBUG

// ################################################################################################

static void* protected_lua_Alloc( void *ud, void *ptr, size_t osize, size_t nsize)
{
	void* p;
	ProtectedAllocator* s = (ProtectedAllocator*) ud;
	MUTEX_LOCK( &s->lock);
	p = s->definition.allocF( s->definition.allocUD, ptr, osize, nsize);
	MUTEX_UNLOCK( &s->lock);
	return p;
}

static int luaG_provide_protected_allocator( lua_State* L)
{
	Universe* U = universe_get( L);
	AllocatorDefinition* def = lua_newuserdatauv( L, sizeof(AllocatorDefinition), 0);
	def->allocF = protected_lua_Alloc;
	def->allocUD = &U->protected_allocator;
	return 1;
}

// Do I need to disable this when compiling for LuaJIT to prevent issues?
void initialize_allocator_function( Universe* U, lua_State* L)
{
	STACK_CHECK( L, 0);
	lua_getfield( L, -1, "allocator");                    // settings allocator|nil|"protected"
	if( !lua_isnil( L, -1))
	{
		// store C function pointer in an internal variable
		U->provide_allocator = lua_tocfunction( L, -1);     // settings allocator
		if( U->provide_allocator != NULL)
		{
			// make sure the function doesn't have upvalues
			char const* upname = lua_getupvalue( L, -1, 1);   // settings allocator upval?
			if( upname != NULL) // should be "" for C functions with upvalues if any
			{
				(void) luaL_error( L, "config.allocator() shouldn't have upvalues");
			}
			// remove this C function from the config table so that it doesn't cause problems
			// when we transfer the config table in newly created Lua states
			lua_pushnil( L);                                  // settings allocator nil
			lua_setfield( L, -3, "allocator");                // settings allocator
		}
		else if( lua_type( L, -1) == LUA_TSTRING)
		{
			// initialize all we need for the protected allocator
			MUTEX_INIT( &U->protected_allocator.lock); // the mutex
			// and the original allocator to call from inside protection by the mutex
			U->protected_allocator.definition.allocF = lua_getallocf( L, &U->protected_allocator.definition.allocUD);
			// before a state is created, this function will be called to obtain the allocator
			U->provide_allocator = luaG_provide_protected_allocator;

			lua_setallocf( L, protected_lua_Alloc, &U->protected_allocator);
		}
	}
	lua_pop( L, 1);                                       // settings
	STACK_END( L, 0);
}

void cleanup_allocator_function( Universe* U, lua_State* L)
{
	// remove the protected allocator, if any
	if( U->protected_allocator.definition.allocF != NULL)
	{
		// install the non-protected allocator
		lua_setallocf( L, U->protected_allocator.definition.allocF, U->protected_allocator.definition.allocUD);
		// release the mutex
		MUTEX_FREE( &U->protected_allocator.lock);
	}
}

// ################################################################################################

void initialize_on_state_create( Universe* U, lua_State* L)
{
	STACK_CHECK( L, 0);
	lua_getfield( L, -1, "on_state_create");              // settings on_state_create|nil
	if( !lua_isnil( L, -1))
	{
		// store C function pointer in an internal variable
		U->on_state_create_func = lua_tocfunction( L, -1);  // settings on_state_create
		if( U->on_state_create_func != NULL)
		{
			// make sure the function doesn't have upvalues
			char const* upname = lua_getupvalue( L, -1, 1);   // settings on_state_create upval?
			if( upname != NULL) // should be "" for C functions with upvalues if any
			{
				(void) luaL_error( L, "on_state_create shouldn't have upvalues");
			}
			// remove this C function from the config table so that it doesn't cause problems
			// when we transfer the config table in newly created Lua states
			lua_pushnil( L);                                  // settings on_state_create nil
			lua_setfield( L, -3, "on_state_create");          // settings on_state_create
		}
		else
		{
			// optim: store marker saying we have such a function in the config table
			U->on_state_create_func = (lua_CFunction) initialize_on_state_create;
		}
	}
	lua_pop( L, 1);                                       // settings
	STACK_END( L, 0);
}

// ################################################################################################

// just like lua_xmove, args are (from, to)
static void copy_one_time_settings( Universe* U, lua_State* L, lua_State* L2)
{
	STACK_GROW( L, 2);
	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);

	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "copy_one_time_settings()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

	REGISTRY_GET( L, CONFIG_REGKEY);                                               // config
	// copy settings from from source to destination registry
	if( luaG_inter_move( U, L, L2, 1, eLM_LaneBody) < 0)                           //                           // config
	{
		(void) luaL_error( L, "failed to copy settings when loading lanes.core");
	}
	// set L2:_R[CONFIG_REGKEY] = settings
	REGISTRY_SET( L2, CONFIG_REGKEY, lua_insert( L2, -2));                                                      //
	STACK_END( L2, 0);
	STACK_END( L, 0);
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
}


/*---=== luaG_newstate ===---*/

static int require_lanes_core( lua_State* L)
{
	// leaves a copy of 'lanes.core' module table on the stack
	luaL_requiref( L, "lanes.core", luaopen_lanes_core, 0);
	return 1;
}


static const luaL_Reg libs[] =
{
	{ LUA_LOADLIBNAME, luaopen_package},
	{ LUA_TABLIBNAME, luaopen_table},
	{ LUA_STRLIBNAME, luaopen_string},
	{ LUA_MATHLIBNAME, luaopen_math},
#ifndef PLATFORM_XBOX // no os/io libs on xbox
	{ LUA_OSLIBNAME, luaopen_os},
	{ LUA_IOLIBNAME, luaopen_io},
#endif // PLATFORM_XBOX
#if LUA_VERSION_NUM >= 503
	{ LUA_UTF8LIBNAME, luaopen_utf8},
#endif
#if LUA_VERSION_NUM >= 502
#ifdef luaopen_bit32
	{ LUA_BITLIBNAME, luaopen_bit32},
#endif
	{ LUA_COLIBNAME, luaopen_coroutine}, // Lua 5.2: coroutine is no longer a part of base!
#else // LUA_VERSION_NUM
	{ LUA_COLIBNAME, NULL},              // Lua 5.1: part of base package
#endif // LUA_VERSION_NUM
	{ LUA_DBLIBNAME, luaopen_debug},
#if defined LUA_JITLIBNAME // building against LuaJIT headers, add some LuaJIT-specific libs
//#pragma message( "supporting JIT base libs")
	{ LUA_BITLIBNAME, luaopen_bit},
	{ LUA_JITLIBNAME, luaopen_jit},
	{ LUA_FFILIBNAME, luaopen_ffi},
#endif // LUA_JITLIBNAME

	{ LUA_DBLIBNAME, luaopen_debug},
	{ "lanes.core", require_lanes_core}, // So that we can open it like any base library (possible since we have access to the init function)
	//
	{ "base", NULL},                     // ignore "base" (already acquired it)
	{ NULL, NULL }
};

static void open1lib( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State* L, char const* name_, size_t len_)
{
	int i;
	for( i = 0; libs[i].name; ++ i)
	{
		if( strncmp( name_, libs[i].name, len_) == 0)
		{
			lua_CFunction libfunc = libs[i].func;
			name_ = libs[i].name; // note that the provided name_ doesn't necessarily ends with '\0', hence len_
			if( libfunc != NULL)
			{
				bool_t const isLanesCore = (libfunc == require_lanes_core) ? TRUE : FALSE; // don't want to create a global for "lanes.core"
				DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "opening %.*s library\n" INDENT_END, (int) len_, name_));
				STACK_CHECK( L, 0);
				// open the library as if through require(), and create a global as well if necessary (the library table is left on the stack)
				luaL_requiref( L, name_, libfunc, !isLanesCore);
				// lanes.core doesn't declare a global, so scan it here and now
				if( isLanesCore == TRUE)
				{
					populate_func_lookup_table( L, -1, name_);
				}
				lua_pop( L, 1);
				STACK_END( L, 0);
			}
			break;
		}
	}
}


static int dummy_writer( lua_State* L, void const* p, size_t sz, void* ud)
{
	(void)L; (void)p; (void)sz; (void) ud; // unused
	return 666;
}


/*
 * differentiation between C, bytecode and JIT-fast functions
 *
 *
 *                   +----------+------------+----------+
 *                   | bytecode | C function | JIT-fast |
 * +-----------------+----------+------------+----------+
 * | lua_topointer   |          |            |          |
 * +-----------------+----------+------------+----------+
 * | lua_tocfunction |  NULL    |            |  NULL    |
 * +-----------------+----------+------------+----------+
 * | lua_dump        |  666     |  1         |  1       |
 * +-----------------+----------+------------+----------+
 */

typedef enum
{
	FST_Bytecode,
	FST_Native,
	FST_FastJIT
} FuncSubType;

FuncSubType luaG_getfuncsubtype( lua_State *L, int _i)
{
	if( lua_tocfunction( L, _i))
	{
		return FST_Native;
	}
	{
		int mustpush = 0, dumpres;
		if( lua_absindex( L, _i) != lua_gettop( L))
		{
			lua_pushvalue( L, _i);
			mustpush = 1;
		}
		// the provided writer fails with code 666
		// therefore, anytime we get 666, this means that lua_dump() attempted a dump
		// all other cases mean this is either a C or LuaJIT-fast function
		dumpres = lua504_dump( L, dummy_writer, NULL, 0);
		lua_pop( L, mustpush);
		if( dumpres == 666)
		{
			return FST_Bytecode;
		}
	}
	return FST_FastJIT;
}

static lua_CFunction luaG_tocfunction( lua_State *L, int _i, FuncSubType *_out)
{
	lua_CFunction p = lua_tocfunction( L, _i);
	*_out = luaG_getfuncsubtype( L, _i);
	return p;
}

// crc64/we of string "LOOKUPCACHE_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( LOOKUPCACHE_REGKEY, 0x837a68dfc6fcb716);

// inspired from tconcat() in ltablib.c
static char const* luaG_pushFQN( lua_State* L, int t, int last, size_t* length)
{
	int i = 1;
	luaL_Buffer b;
	STACK_CHECK( L, 0);
	luaL_buffinit( L, &b);
	for( ; i < last; ++ i)
	{
		lua_rawgeti( L, t, i);
		luaL_addvalue( &b);
		luaL_addlstring(&b, "/", 1);
	}
	if( i == last)  // add last value (if interval was not empty)
	{
		lua_rawgeti( L, t, i);
		luaL_addvalue( &b);
	}
	luaL_pushresult( &b);
	STACK_END( L, 1);
	return lua_tolstring( L, -1, length);
}

/*
 * receives 2 arguments: a name k and an object o
 * add two entries ["fully.qualified.name"] = o
 * and             [o] = "fully.qualified.name"
 * where <o> is either a table or a function
 * if we already had an entry of type [o] = ..., replace the name if the new one is shorter
 * pops the processed object from the stack
 */
static void update_lookup_entry( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State* L, int _ctx_base, int _depth)
{
	// slot 1 in the stack contains the table that receives everything we found
	int const dest = _ctx_base;
	// slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
	int const fqn = _ctx_base + 1;

	size_t prevNameLength, newNameLength;
	char const* prevName;
	DEBUGSPEW_CODE( char const *newName);
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "update_lookup_entry()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

	STACK_CHECK( L, 0);
	// first, raise an error if the function is already known
	lua_pushvalue( L, -1);                                                                // ... {bfc} k o o
	lua_rawget( L, dest);                                                                 // ... {bfc} k o name?
	prevName = lua_tolstring( L, -1, &prevNameLength); // NULL if we got nil (first encounter of this object)
	// push name in fqn stack (note that concatenation will crash if name is a not string or a number)
	lua_pushvalue( L, -3);                                                                // ... {bfc} k o name? k
	ASSERT_L( lua_type( L, -1) == LUA_TNUMBER || lua_type( L, -1) == LUA_TSTRING);
	++ _depth;
	lua_rawseti( L, fqn, _depth);                                                         // ... {bfc} k o name?
	// generate name
	DEBUGSPEW_CODE( newName =) luaG_pushFQN( L, fqn, _depth, &newNameLength);             // ... {bfc} k o name? "f.q.n"
	// Lua 5.2 introduced a hash randomizer seed which causes table iteration to yield a different key order
	// on different VMs even when the tables are populated the exact same way.
	// When Lua is built with compatibility options (such as LUA_COMPAT_ALL),
	// this causes several base libraries to register functions under multiple names.
	// This, with the randomizer, can cause the first generated name of an object to be different on different VMs,
	// which breaks function transfer.
	// Also, nothing prevents any external module from exposing a given object under several names, so...
	// Therefore, when we encounter an object for which a name was previously registered, we need to select the names
	// based on some sorting order so that we end up with the same name in all databases whatever order the table walk yielded
	if( prevName != NULL && (prevNameLength < newNameLength || lua_lessthan( L, -2, -1)))
	{
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%s '%s' remained named '%s'\n" INDENT_END, lua_typename( L, lua_type( L, -3)), newName, prevName));
		// the previous name is 'smaller' than the one we just generated: keep it!
		lua_pop( L, 3);                                                                     // ... {bfc} k
	}
	else
	{
		// the name we generated is either the first one, or a better fit for our purposes
		if( prevName)
		{
			// clear the previous name for the database to avoid clutter
			lua_insert( L, -2);                                                               // ... {bfc} k o "f.q.n" prevName
			// t[prevName] = nil
			lua_pushnil( L);                                                                  // ... {bfc} k o "f.q.n" prevName nil
			lua_rawset( L, dest);                                                             // ... {bfc} k o "f.q.n"
		}
		else
		{
			lua_remove( L, -2);                                                               // ... {bfc} k o "f.q.n"
		}
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%s '%s'\n" INDENT_END, lua_typename( L, lua_type( L, -2)), newName));
		// prepare the stack for database feed
		lua_pushvalue( L, -1);                                                              // ... {bfc} k o "f.q.n" "f.q.n"
		lua_pushvalue( L, -3);                                                              // ... {bfc} k o "f.q.n" "f.q.n" o
		ASSERT_L( lua_rawequal( L, -1, -4));
		ASSERT_L( lua_rawequal( L, -2, -3));
		// t["f.q.n"] = o
		lua_rawset( L, dest);                                                               // ... {bfc} k o "f.q.n"
		// t[o] = "f.q.n"
		lua_rawset( L, dest);                                                               // ... {bfc} k
		// remove table name from fqn stack
		lua_pushnil( L);                                                                    // ... {bfc} k nil
		lua_rawseti( L, fqn, _depth);                                                       // ... {bfc} k
	}
	-- _depth;
	STACK_END( L, -1);
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
}

static void populate_func_lookup_table_recur( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State* L, int _ctx_base, int _i, int _depth)
{
	lua_Integer visit_count;
	// slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
	int const fqn = _ctx_base + 1;
	// slot 3 contains a cache that stores all already visited tables to avoid infinite recursion loops
	int const cache = _ctx_base + 2;
	// we need to remember subtables to process them after functions encountered at the current depth (breadth-first search)
	int const breadth_first_cache = lua_gettop( L) + 1;
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "populate_func_lookup_table_recur()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

	STACK_GROW( L, 6);
	// slot _i contains a table where we search for functions (or a full userdata with a metatable)
	STACK_CHECK( L, 0);                                                                       // ... {_i}

	// if object is a userdata, replace it by its metatable
	if( lua_type( L, _i) == LUA_TUSERDATA)
	{
		lua_getmetatable( L, _i);                                                               // ... {_i} mt
		lua_replace( L, _i);                                                                    // ... {_i}
	}

	// if table is already visited, we are done
	lua_pushvalue( L, _i);                                                                    // ... {_i} {}
	lua_rawget( L, cache);                                                                    // ... {_i} nil|n
	visit_count = lua_tointeger( L, -1); // 0 if nil, else n
	lua_pop( L, 1);                                                                           // ... {_i}
	STACK_MID( L, 0);
	if( visit_count > 0)
	{
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "already visited\n" INDENT_END));
		DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
		return;
	}

	// remember we visited this table (1-visit count)
	lua_pushvalue( L, _i);                                                                    // ... {_i} {}
	lua_pushinteger( L, visit_count + 1);                                                     // ... {_i} {} 1
	lua_rawset( L, cache);                                                                    // ... {_i}
	STACK_MID( L, 0);

	// this table is at breadth_first_cache index
	lua_newtable( L);                                                                         // ... {_i} {bfc}
	ASSERT_L( lua_gettop( L) == breadth_first_cache);
	// iterate over all entries in the processed table
	lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
	while( lua_next( L, _i) != 0)                                                             // ... {_i} {bfc} k v
	{
		// just for debug, not actually needed
		//char const* key = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : "not a string";
		// subtable: process it recursively
		if( lua_istable( L, -1))                                                                // ... {_i} {bfc} k {}
		{
			// increment visit count to make sure we will actually scan it at this recursive level
			lua_pushvalue( L, -1);                                                                // ... {_i} {bfc} k {} {}
			lua_pushvalue( L, -1);                                                                // ... {_i} {bfc} k {} {} {}
			lua_rawget( L, cache);                                                                // ... {_i} {bfc} k {} {} n?
			visit_count = lua_tointeger( L, -1) + 1; // 1 if we got nil, else n+1
			lua_pop( L, 1);                                                                       // ... {_i} {bfc} k {} {}
			lua_pushinteger( L, visit_count);                                                     // ... {_i} {bfc} k {} {} n
			lua_rawset( L, cache);                                                                // ... {_i} {bfc} k {}
			// store the table in the breadth-first cache
			lua_pushvalue( L, -2);                                                                // ... {_i} {bfc} k {} k
			lua_pushvalue( L, -2);                                                                // ... {_i} {bfc} k {} k {}
			lua_rawset( L, breadth_first_cache);                                                  // ... {_i} {bfc} k {}
			// generate a name, and if we already had one name, keep whichever is the shorter
			update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, _depth);                 // ... {_i} {bfc} k
		}
		else if( lua_isfunction( L, -1) && (luaG_getfuncsubtype( L, -1) != FST_Bytecode))       // ... {_i} {bfc} k func
		{
			// generate a name, and if we already had one name, keep whichever is the shorter
			update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, _depth);                 // ... {_i} {bfc} k
		}
		else
		{
			lua_pop( L, 1);                                                                       // ... {_i} {bfc} k
		}
		STACK_MID( L, 2);
	}
	// now process the tables we encountered at that depth
	++ _depth;
	lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
	while( lua_next( L, breadth_first_cache) != 0)                                            // ... {_i} {bfc} k {}
	{
		DEBUGSPEW_CODE( char const* key = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : "not a string");
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "table '%s'\n" INDENT_END, key));
		DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
		// un-visit this table in case we do need to process it
		lua_pushvalue( L, -1);                                                                  // ... {_i} {bfc} k {} {}
		lua_rawget( L, cache);                                                                  // ... {_i} {bfc} k {} n
		ASSERT_L( lua_type( L, -1) == LUA_TNUMBER);
		visit_count = lua_tointeger( L, -1) - 1;
		lua_pop( L, 1);                                                                         // ... {_i} {bfc} k {}
		lua_pushvalue( L, -1);                                                                  // ... {_i} {bfc} k {} {}
		if( visit_count > 0)
		{
			lua_pushinteger( L, visit_count);                                                     // ... {_i} {bfc} k {} {} n
		}
		else
		{
			lua_pushnil( L);                                                                      // ... {_i} {bfc} k {} {} nil
		}
		lua_rawset( L, cache);                                                                  // ... {_i} {bfc} k {}
		// push table name in fqn stack (note that concatenation will crash if name is a not string!)
		lua_pushvalue( L, -2);                                                                  // ... {_i} {bfc} k {} k
		lua_rawseti( L, fqn, _depth);                                                           // ... {_i} {bfc} k {}
		populate_func_lookup_table_recur( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, lua_gettop( L), _depth);
		lua_pop( L, 1);                                                                         // ... {_i} {bfc} k
		STACK_MID( L, 2);
		DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
	}
	// remove table name from fqn stack
	lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
	lua_rawseti( L, fqn, _depth);                                                             // ... {_i} {bfc}
	-- _depth;
	// we are done with our cache
	lua_pop( L, 1);                                                                           // ... {_i}
	STACK_END( L, 0);
	// we are done                                                                            // ... {_i} {bfc}
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
}

/*
 * create a "fully.qualified.name" <-> function equivalence database
 */
void populate_func_lookup_table( lua_State* L, int _i, char const* name_)
{
	int const ctx_base = lua_gettop( L) + 1;
	int const in_base = lua_absindex( L, _i);
	int start_depth = 0;
	DEBUGSPEW_CODE( Universe* U = universe_get( L));
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%p: populate_func_lookup_table('%s')\n" INDENT_END, L, name_ ? name_ : "NULL"));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
	STACK_GROW( L, 3);
	STACK_CHECK( L, 0);
	REGISTRY_GET( L, LOOKUP_REGKEY);                                               // {}
	STACK_MID( L, 1);
	ASSERT_L( lua_istable( L, -1));
	if( lua_type( L, in_base) == LUA_TFUNCTION) // for example when a module is a simple function
	{
		name_ = name_ ? name_ : "NULL";
		lua_pushvalue( L, in_base);                                                  // {} f
		lua_pushstring( L, name_);                                                   // {} f _name
		lua_rawset( L, -3);                                                          // {}
		lua_pushstring( L, name_);                                                   // {} _name
		lua_pushvalue( L, in_base);                                                  // {} _name f
		lua_rawset( L, -3);                                                          // {}
		lua_pop( L, 1);                                                              //
	}
	else if( lua_type( L, in_base) == LUA_TTABLE)
	{
		lua_newtable( L);                                                            // {} {fqn}
		if( name_)
		{
			STACK_MID( L, 2);
			lua_pushstring( L, name_);                                                 // {} {fqn} "name"
			// generate a name, and if we already had one name, keep whichever is the shorter
			lua_pushvalue( L, in_base);                                           // {} {fqn} "name" t
			update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, ctx_base, start_depth);  // {} {fqn} "name"
			// don't forget to store the name at the bottom of the fqn stack
			++ start_depth;
			lua_rawseti( L, -2, start_depth);                                          // {} {fqn}
			STACK_MID( L, 2);
		}
		// retrieve the cache, create it if we haven't done it yet
		REGISTRY_GET( L, LOOKUPCACHE_REGKEY);                                        // {} {fqn} {cache}?
		if( lua_isnil( L, -1))
		{
			lua_pop( L, 1);                                                            // {} {fqn}
			lua_newtable( L);                                                          // {} {fqn} {cache}
			REGISTRY_SET( L, LOOKUPCACHE_REGKEY, lua_pushvalue( L, -2));
			STACK_MID( L, 3);
		}
		// process everything we find in that table, filling in lookup data for all functions and tables we see there
		populate_func_lookup_table_recur( DEBUGSPEW_PARAM_COMMA( U) L, ctx_base, in_base, start_depth);
		lua_pop( L, 3);
	}
	else
	{
		lua_pop( L, 1);                                                         //
		(void) luaL_error( L, "unsupported module type %s", lua_typename( L, lua_type( L, in_base)));
	}
	STACK_END( L, 0);
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
}

void call_on_state_create( Universe* U, lua_State* L, lua_State* from_, LookupMode mode_)
{
	if( U->on_state_create_func != NULL)
	{
		STACK_CHECK( L, 0);
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "calling on_state_create()\n" INDENT_END));
		if( U->on_state_create_func != (lua_CFunction) initialize_on_state_create)
		{
			// C function: recreate a closure in the new state, bypassing the lookup scheme
			lua_pushcfunction( L, U->on_state_create_func);                             // on_state_create()
		}
		else // Lua function located in the config table, copied when we opened "lanes.core"
		{
			if( mode_ != eLM_LaneBody)
			{
				// if attempting to call in a keeper state, do nothing because the function doesn't exist there
				// this doesn't count as an error though
				return;
			}
			REGISTRY_GET( L, CONFIG_REGKEY);                                            // {}
			STACK_MID( L, 1);
			lua_getfield( L, -1, "on_state_create");                                    // {} on_state_create()
			lua_remove( L, -2);                                                         // on_state_create()
		}
		STACK_MID( L, 1);
		// capture error and raise it in caller state
		if( lua_pcall( L, 0, 0, 0) != LUA_OK)
		{
			luaL_error( from_, "on_state_create failed: \"%s\"", lua_isstring( L, -1) ? lua_tostring( L, -1) : lua_typename( L, lua_type( L, -1)));
		}
		STACK_END( L, 0);
	}
}

lua_State* create_state( Universe* U, lua_State* from_)
{
	lua_State* L;
	if( U->provide_allocator != NULL)
	{
		lua_pushcclosure( from_, U->provide_allocator, 0);
		lua_call( from_, 0, 1);
		{
			AllocatorDefinition* def = lua_touserdata( from_, -1);
			L = lua_newstate( def->allocF, def->allocUD);
		}
		lua_pop( from_, 1);
	}
	else
	{
		L = luaL_newstate();
	}

	if( L == NULL)
	{
		(void) luaL_error( from_, "luaG_newstate() failed while creating state; out of memory");
	}
	return L;
}

/*
 * Like 'luaL_openlibs()' but allows the set of libraries be selected
 *
 *   NULL    no libraries, not even base
 *   ""      base library only
 *   "io,string"     named libraries
 *   "*"     all libraries
 *
 * Base ("unpack", "print" etc.) is always added, unless 'libs' is NULL.
 *
 * *NOT* called for keeper states!
 *
 */
lua_State* luaG_newstate( Universe* U, lua_State* from_, char const* libs_)
{
	lua_State* L = create_state( U, from_);

	STACK_GROW( L, 2);
	STACK_CHECK_ABS( L, 0);

	// copy the universe as a light userdata (only the master state holds the full userdata)
	// that way, if Lanes is required in this new state, we'll know we are part of this universe
	universe_store( L, U);
	STACK_MID( L, 0);

	// we'll need this every time we transfer some C function from/to this state
	REGISTRY_SET( L, LOOKUP_REGKEY, lua_newtable( L));
	STACK_MID( L, 0);

	// neither libs (not even 'base') nor special init func: we are done
	if( libs_ == NULL && U->on_state_create_func == NULL)
	{
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "luaG_newstate(NULL)\n" INDENT_END));
		return L;
	}

	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "luaG_newstate()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

	// copy settings (for example because it may contain a Lua on_state_create function)
	copy_one_time_settings( U, from_, L);

	// 'lua.c' stops GC during initialization so perhaps its a good idea. :)
	lua_gc( L, LUA_GCSTOP, 0);

		
		// Anything causes 'base' to be taken in
	//
	if( libs_ != NULL)
	{
		// special "*" case (mainly to help with LuaJIT compatibility)
		// as we are called from luaopen_lanes_core() already, and that would deadlock
		if( libs_[0] == '*' && libs_[1] == 0)
		{
			DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "opening ALL standard libraries\n" INDENT_END));
			luaL_openlibs( L);
			// don't forget lanes.core for regular lane states
			open1lib( DEBUGSPEW_PARAM_COMMA( U) L, "lanes.core", 10);
			libs_ = NULL; // done with libs
		}
		else
		{
			DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "opening base library\n" INDENT_END));
#if LUA_VERSION_NUM >= 502
			// open base library the same way as in luaL_openlibs()
			luaL_requiref( L, "_G", luaopen_base, 1);
			lua_pop( L, 1);
#else // LUA_VERSION_NUM
			lua_pushcfunction( L, luaopen_base);
			lua_pushstring( L, "");
			lua_call( L, 1, 0);
#endif // LUA_VERSION_NUM
		}
	}
	STACK_END( L, 0);

	// scan all libraries, open them one by one
	if( libs_)
	{
		char const* p;
		unsigned int len = 0;
		for( p = libs_; *p; p += len)
		{
			// skip delimiters ('.' can be part of name for "lanes.core")
			while( *p && !isalnum( *p) && *p != '.')
				++ p;
			// skip name
			len = 0;
			while( isalnum( p[len]) || p[len] == '.')
				++ len;
			// open library
			open1lib( DEBUGSPEW_PARAM_COMMA( U) L, p, len);
		}
	}
	lua_gc( L, LUA_GCRESTART, 0);

	serialize_require( DEBUGSPEW_PARAM_COMMA( U) L);

	// call this after the base libraries are loaded and GC is restarted
	// will raise an error in from_ in case of problem
	call_on_state_create( U, L, from_, eLM_LaneBody);

	STACK_CHECK( L, 0);
	// after all this, register everything we find in our name<->function database
	lua_pushglobaltable( L); // Lua 5.2 no longer has LUA_GLOBALSINDEX: we must push globals table on the stack
	populate_func_lookup_table( L, -1, NULL);

#if 0 && USE_DEBUG_SPEW
	// dump the lookup database contents
	lua_getfield( L, LUA_REGISTRYINDEX, LOOKUP_REGKEY);                       // {}
	lua_pushnil( L);                                                          // {} nil
	while( lua_next( L, -2))                                                  // {} k v
	{
		lua_getglobal( L, "print");                                             // {} k v print
		lua_pushlstring( L, debugspew_indent, U->debugspew_indent_depth);       // {} k v print " "
		lua_pushvalue( L, -4);                                                  // {} k v print " " k
		lua_pushvalue( L, -4);                                                  // {} k v print " " k v
		lua_call( L, 3, 0);                                                     // {} k v
		lua_pop( L, 1);                                                         // {} k
	}
	lua_pop( L, 1);                                                           // {}
#endif // USE_DEBUG_SPEW

	lua_pop( L, 1);
	STACK_END( L, 0);
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
	return L;
}




/*---=== Inter-state copying ===---*/

// crc64/we of string "REG_MTID" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( REG_MTID, 0x2e68f9b4751584dc);

/*
* Get a unique ID for metatable at [i].
*/
static lua_Integer get_mt_id( Universe* U, lua_State* L, int i)
{
	lua_Integer id;

	i = lua_absindex( L, i);

	STACK_GROW( L, 3);

	STACK_CHECK( L, 0);
	push_registry_subtable( L, REG_MTID);        // ... _R[REG_MTID]
	lua_pushvalue( L, i);                        // ... _R[REG_MTID] {mt}
	lua_rawget( L, -2);                          // ... _R[REG_MTID] mtk?

	id = lua_tointeger( L, -1);    // 0 for nil
	lua_pop( L, 1);                              // ... _R[REG_MTID]
	STACK_MID( L, 1);

	if( id == 0)
	{
		MUTEX_LOCK( &U->mtid_lock);
		id = ++ U->last_mt_id;
		MUTEX_UNLOCK( &U->mtid_lock);

		/* Create two-way references: id_uint <-> table
		*/
		lua_pushvalue( L, i);                      // ... _R[REG_MTID] {mt}
		lua_pushinteger( L, id);                   // ... _R[REG_MTID] {mt} id
		lua_rawset( L, -3);                        // ... _R[REG_MTID]

		lua_pushinteger( L, id);                   // ... _R[REG_MTID] id
		lua_pushvalue( L, i);                      // ... _R[REG_MTID] id {mt}
		lua_rawset( L, -3);                        // ... _R[REG_MTID]
	}
	lua_pop( L, 1);                              // ...

	STACK_END( L, 0);

	return id;
}


static int buf_writer( lua_State *L, const void* b, size_t n, void* B ) {
  (void)L;
  luaL_addlstring((luaL_Buffer*) B, (const char *)b, n);
  return 0;
}


// function sentinel used to transfer native functions from/to keeper states
static int func_lookup_sentinel( lua_State* L)
{
	return luaL_error( L, "function lookup sentinel for %s, should never be called", lua_tostring( L, lua_upvalueindex( 1)));
}


// function sentinel used to transfer native table from/to keeper states
static int table_lookup_sentinel( lua_State* L)
{
	return luaL_error( L, "table lookup sentinel for %s, should never be called", lua_tostring( L, lua_upvalueindex( 1)));
}

// function sentinel used to transfer cloned full userdata from/to keeper states
static int userdata_clone_sentinel( lua_State* L)
{
	return luaL_error( L, "userdata clone sentinel for %s, should never be called", lua_tostring( L, lua_upvalueindex( 1)));
}

/*
 * retrieve the name of a function/table in the lookup database
 */
static char const* find_lookup_name( lua_State* L, uint_t i, LookupMode mode_, char const* upName_, size_t* len_)
{
	DEBUGSPEW_CODE( Universe* const U = universe_get( L));
	char const* fqn;
	ASSERT_L( lua_isfunction( L, i) || lua_istable( L, i));  // ... v ...
	STACK_CHECK( L, 0);
	STACK_GROW( L, 3); // up to 3 slots are necessary on error
	if( mode_ == eLM_FromKeeper)
	{
		lua_CFunction f = lua_tocfunction( L, i); // should *always* be func_lookup_sentinel or table_lookup_sentinel!
		if( f == func_lookup_sentinel || f == table_lookup_sentinel || f == userdata_clone_sentinel)
		{
			lua_getupvalue( L, i, 1);                            // ... v ... "f.q.n"
		}
		else
		{
			// if this is not a sentinel, this is some user-created table we wanted to lookup
			ASSERT_L( NULL == f && lua_istable( L, i));
			// push anything that will convert to NULL string
			lua_pushnil( L);                                     // ... v ... nil
		}
	}
	else
	{
		// fetch the name from the source state's lookup table
		REGISTRY_GET( L, LOOKUP_REGKEY);                       // ... v ... {}
		STACK_MID( L, 1);
		ASSERT_L( lua_istable( L, -1));
		lua_pushvalue( L, i);                                  // ... v ... {} v
		lua_rawget( L, -2);                                    // ... v ... {} "f.q.n"
	}
	fqn = lua_tolstring( L, -1, len_);
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "function [C] %s \n" INDENT_END, fqn));
	// popping doesn't invalidate the pointer since this is an interned string gotten from the lookup database
	lua_pop( L, (mode_ == eLM_FromKeeper) ? 1 : 2);          // ... v ...
	STACK_MID( L, 0);
	if( NULL == fqn && !lua_istable( L, i)) // raise an error if we try to send an unknown function (but not for tables)
	{
		char const *from, *typewhat, *what, *gotchaA, *gotchaB;
		// try to discover the name of the function we want to send
		lua_getglobal( L, "decoda_name");                      // ... v ... decoda_name
		from = lua_tostring( L, -1);
		lua_pushcfunction( L, luaG_nameof);                    // ... v ... decoda_name luaG_nameof
		lua_pushvalue( L, i);                                  // ... v ... decoda_name luaG_nameof t
		lua_call( L, 1, 2);                                    // ... v ... decoda_name "type" "name"|nil
		typewhat = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : luaL_typename( L, -2);
		// second return value can be nil if the table was not found
		// probable reason: the function was removed from the source Lua state before Lanes was required.
		if( lua_isnil( L, -1))
		{
			gotchaA = " referenced by";
			gotchaB = "\n(did you remove it from the source Lua state before requiring Lanes?)";
			what = upName_;
		}
		else
		{
			gotchaA = "";
			gotchaB = "";
			what = (lua_type( L, -1) == LUA_TSTRING) ? lua_tostring( L, -1) : luaL_typename( L, -1);
		}
		(void) luaL_error( L, "%s%s '%s' not found in %s origin transfer database.%s", typewhat, gotchaA, what, from ? from : "main", gotchaB);
		*len_ = 0;
		return NULL;
	}
	STACK_END( L, 0);
	return fqn;
}


/*
 * Push a looked-up table, or nothing if we found nothing
 */
static bool_t lookup_table( lua_State* L2, lua_State* L, uint_t i, LookupMode mode_, char const* upName_)
{
	// get the name of the table we want to send
	size_t len;
	char const* fqn = find_lookup_name( L, i, mode_, upName_, &len);
	if( NULL == fqn) // name not found, it is some user-created table
	{
		return FALSE;
	}
	// push the equivalent table in the destination's stack, retrieved from the lookup table
	STACK_CHECK( L2, 0);                                        // L                          // L2
	STACK_GROW( L2, 3); // up to 3 slots are necessary on error
	switch( mode_)
	{
		default: // shouldn't happen, in theory...
		(void) luaL_error( L, "internal error: unknown lookup mode");
		return FALSE;

		case eLM_ToKeeper:
		// push a sentinel closure that holds the lookup name as upvalue
		lua_pushlstring( L2, fqn, len);                                                      // "f.q.n"
		lua_pushcclosure( L2, table_lookup_sentinel, 1);                                     // f
		break;

		case eLM_LaneBody:
		case eLM_FromKeeper:
		REGISTRY_GET( L2, LOOKUP_REGKEY);                                                    // {}
		STACK_MID( L2, 1);
		ASSERT_L( lua_istable( L2, -1));
		lua_pushlstring( L2, fqn, len);                                                      // {} "f.q.n"
		lua_rawget( L2, -2);                                                                 // {} t
		// we accept destination lookup failures in the case of transfering the Lanes body function (this will result in the source table being cloned instead)
		// but not when we extract something out of a keeper, as there is nothing to clone!
		if( lua_isnil( L2, -1) && mode_ == eLM_LaneBody)
		{
			lua_pop( L2, 2);                                                                   //
			STACK_MID( L2, 0);
			return FALSE;
		}
		else if( !lua_istable( L2, -1))
		{
			char const* from, *to;
			lua_getglobal( L, "decoda_name");                    // ... t ... decoda_name
			from = lua_tostring( L, -1);
			lua_pop( L, 1);                                      // ... t ...
			lua_getglobal( L2, "decoda_name");                                                 // {} t decoda_name
			to = lua_tostring( L2, -1);
			lua_pop( L2, 1);                                                                   // {} t
			// when mode_ == eLM_FromKeeper, L is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
			(void) luaL_error(
				(mode_ == eLM_FromKeeper) ? L2 : L
				, "INTERNAL ERROR IN %s: table '%s' not found in %s destination transfer database."
				, from ? from : "main"
				, fqn
				, to ? to : "main"
			);
			return FALSE;
		}
		lua_remove( L2, -2);                                                                 // t
		break;
	}
	STACK_END( L2, 1);
	return TRUE;
}


/* 
 * Check if we've already copied the same table from 'L', and
 * reuse the old copy. This allows table upvalues shared by multiple
 * local functions to point to the same table, also in the target.
 *
 * Always pushes a table to 'L2'.
 *
 * Returns TRUE if the table was cached (no need to fill it!); FALSE if
 * it's a virgin.
 */
static bool_t push_cached_table( lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i)
{
	bool_t not_found_in_cache;                                                                     // L2
	DECLARE_CONST_UNIQUE_KEY( p, lua_topointer( L, i));

	ASSERT_L( L2_cache_i != 0);
	STACK_GROW( L2, 3);
	STACK_CHECK( L2, 0);

	// We don't need to use the from state ('L') in ID since the life span
	// is only for the duration of a copy (both states are locked).
	// push a light userdata uniquely representing the table
	push_unique_key( L2, p);                                                                       // ... p

	//fprintf( stderr, "<< ID: %s >>\n", lua_tostring( L2, -1));

	lua_rawget( L2, L2_cache_i);                                                                   // ... {cached|nil}
	not_found_in_cache = lua_isnil( L2, -1);
	if( not_found_in_cache)
	{
		lua_pop( L2, 1);                                                                             // ...
		lua_newtable( L2);                                                                           // ... {}
		push_unique_key( L2, p);                                                                     // ... {} p
		lua_pushvalue( L2, -2);                                                                      // ... {} p {}
		lua_rawset( L2, L2_cache_i);                                                                 // ... {}
	}
	STACK_END( L2, 1);
	ASSERT_L( lua_istable( L2, -1));
	return !not_found_in_cache;
}


/*
 * Return some name helping to identify an object
 */
static int discover_object_name_recur( lua_State* L, int shortest_, int depth_)
{
	int const what = 1;                                     // o "r" {c} {fqn} ... {?}
	int const result = 2;
	int const cache = 3;
	int const fqn = 4;
	// no need to scan this table if the name we will discover is longer than one we already know
	if( shortest_ <= depth_ + 1)
	{
		return shortest_;
	}
	STACK_GROW( L, 3);
	STACK_CHECK( L, 0);
	// stack top contains the table to search in
	lua_pushvalue( L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
	lua_rawget( L, cache);                                  // o "r" {c} {fqn} ... {?} nil/1
	// if table is already visited, we are done
	if( !lua_isnil( L, -1))
	{
		lua_pop( L, 1);                                       // o "r" {c} {fqn} ... {?}
		return shortest_;
	}
	// examined table is not in the cache, add it now
	lua_pop( L, 1);                                         // o "r" {c} {fqn} ... {?}
	lua_pushvalue( L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
	lua_pushinteger( L, 1);                                 // o "r" {c} {fqn} ... {?} {?} 1
	lua_rawset( L, cache);                                  // o "r" {c} {fqn} ... {?}
	// scan table contents
	lua_pushnil( L);                                        // o "r" {c} {fqn} ... {?} nil
	while( lua_next( L, -2))                                // o "r" {c} {fqn} ... {?} k v
	{
		//char const *const strKey = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : NULL; // only for debugging
		//lua_Number const numKey = (lua_type( L, -2) == LUA_TNUMBER) ? lua_tonumber( L, -2) : -6666; // only for debugging
		STACK_MID( L, 2);
		// append key name to fqn stack
		++ depth_;
		lua_pushvalue( L, -2);                                // o "r" {c} {fqn} ... {?} k v k
		lua_rawseti( L, fqn, depth_);                         // o "r" {c} {fqn} ... {?} k v
		if( lua_rawequal( L, -1, what)) // is it what we are looking for?
		{
			STACK_MID( L, 2);
			// update shortest name
			if( depth_ < shortest_)
			{
				shortest_ = depth_;
				luaG_pushFQN( L, fqn, depth_, NULL);              // o "r" {c} {fqn} ... {?} k v "fqn"
				lua_replace( L, result);                          // o "r" {c} {fqn} ... {?} k v
			}
			// no need to search further at this level
			lua_pop( L, 2);                                     // o "r" {c} {fqn} ... {?}
			STACK_MID( L, 0);
			break;
		}
		switch( lua_type( L, -1))                             // o "r" {c} {fqn} ... {?} k v
		{
			default: // nil, boolean, light userdata, number and string aren't identifiable
			break;

			case LUA_TTABLE:                                    // o "r" {c} {fqn} ... {?} k {}
			STACK_MID( L, 2);
			shortest_ = discover_object_name_recur( L, shortest_, depth_);
			// search in the table's metatable too
			if( lua_getmetatable( L, -1))                       // o "r" {c} {fqn} ... {?} k {} {mt}
			{
				if( lua_istable( L, -1))
				{
					++ depth_;
					lua_pushliteral( L, "__metatable");             // o "r" {c} {fqn} ... {?} k {} {mt} "__metatable"
					lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k {} {mt}
					shortest_ = discover_object_name_recur( L, shortest_, depth_);
					lua_pushnil( L);                                // o "r" {c} {fqn} ... {?} k {} {mt} nil
					lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k {} {mt}
					-- depth_;
				}
				lua_pop( L, 1);                                   // o "r" {c} {fqn} ... {?} k {}
			}
			STACK_MID( L, 2);
			break;

			case LUA_TTHREAD:                                   // o "r" {c} {fqn} ... {?} k T
			// TODO: explore the thread's stack frame looking for our culprit?
			break;

			case LUA_TUSERDATA:                                 // o "r" {c} {fqn} ... {?} k U
			STACK_MID( L, 2);
			// search in the object's metatable (some modules are built that way)
			if( lua_getmetatable( L, -1))                       // o "r" {c} {fqn} ... {?} k U {mt}
			{
				if( lua_istable( L, -1))
				{
					++ depth_;
					lua_pushliteral( L, "__metatable");             // o "r" {c} {fqn} ... {?} k U {mt} "__metatable"
					lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k U {mt}
					shortest_ = discover_object_name_recur( L, shortest_, depth_);
					lua_pushnil( L);                                // o "r" {c} {fqn} ... {?} k U {mt} nil
					lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k U {mt}
					-- depth_;
				}
				lua_pop( L, 1);                                   // o "r" {c} {fqn} ... {?} k U
			}
			STACK_MID( L, 2);
			// search in the object's uservalues
			{
				int uvi = 1;
				while( lua_getiuservalue( L, -1, uvi) != LUA_TNONE) // o "r" {c} {fqn} ... {?} k U {u}
				{
					if( lua_istable( L, -1)) // if it is a table, look inside
					{
						++ depth_;
						lua_pushliteral( L, "uservalue");               // o "r" {c} {fqn} ... {?} k v {u} "uservalue"
						lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k v {u}
						shortest_ = discover_object_name_recur( L, shortest_, depth_);
						lua_pushnil( L);                                // o "r" {c} {fqn} ... {?} k v {u} nil
						lua_rawseti( L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k v {u}
						-- depth_;
					}
					lua_pop( L, 1);                                   // o "r" {c} {fqn} ... {?} k U
					++ uvi;
				}
				// when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
				lua_pop( L, 1);                                     // o "r" {c} {fqn} ... {?} k U
			}
			STACK_MID( L, 2);
			break;
		}
		// make ready for next iteration
		lua_pop( L, 1);                                       // o "r" {c} {fqn} ... {?} k
		// remove name from fqn stack
		lua_pushnil( L);                                      // o "r" {c} {fqn} ... {?} k nil
		lua_rawseti( L, fqn, depth_);                         // o "r" {c} {fqn} ... {?} k
		STACK_MID( L, 1);
		-- depth_;
	}                                                       // o "r" {c} {fqn} ... {?}
	STACK_MID( L, 0);
	// remove the visited table from the cache, in case a shorter path to the searched object exists
	lua_pushvalue( L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
	lua_pushnil( L);                                        // o "r" {c} {fqn} ... {?} {?} nil
	lua_rawset( L, cache);                                  // o "r" {c} {fqn} ... {?}
	STACK_END( L, 0);
	return shortest_;
}


/*
 * "type", "name" = lanes.nameof( o)
 */
int luaG_nameof( lua_State* L)
{
	int what = lua_gettop( L);
	if( what > 1)
	{
		luaL_argerror( L, what, "too many arguments.");
	}

	// nil, boolean, light userdata, number and string aren't identifiable
	if( lua_type( L, 1) < LUA_TTABLE)
	{
		lua_pushstring( L, luaL_typename( L, 1));             // o "type"
		lua_insert( L, -2);                                   // "type" o
		return 2;
	}

	STACK_GROW( L, 4);
	STACK_CHECK( L, 0);
	// this slot will contain the shortest name we found when we are done
	lua_pushnil( L);                                        // o nil
	// push a cache that will contain all already visited tables
	lua_newtable( L);                                       // o nil {c}
	// push a table whose contents are strings that, when concatenated, produce unique name
	lua_newtable( L);                                       // o nil {c} {fqn}
	lua_pushliteral( L, "_G");                              // o nil {c} {fqn} "_G"
	lua_rawseti( L, -2, 1);                                 // o nil {c} {fqn}
	// this is where we start the search
	lua_pushglobaltable( L);                                // o nil {c} {fqn} _G
	(void) discover_object_name_recur( L, 6666, 1);
	if( lua_isnil( L, 2)) // try again with registry, just in case...
	{
		lua_pop( L, 1);                                       // o nil {c} {fqn}
		lua_pushliteral( L, "_R");                            // o nil {c} {fqn} "_R"
		lua_rawseti( L, -2, 1);                               // o nil {c} {fqn}
		lua_pushvalue( L, LUA_REGISTRYINDEX);                 // o nil {c} {fqn} _R
		(void) discover_object_name_recur( L, 6666, 1);
	}
	lua_pop( L, 3);                                         // o "result"
	STACK_END( L, 1);
	lua_pushstring( L, luaL_typename( L, 1));               // o "result" "type"
	lua_replace( L, -3);                                    // "type" "result"
	return 2;
}


/*
 * Push a looked-up native/LuaJIT function.
 */
static void lookup_native_func( lua_State* L2, lua_State* L, uint_t i, LookupMode mode_, char const* upName_)
{
	// get the name of the function we want to send
	size_t len;
	char const* fqn = find_lookup_name( L, i, mode_, upName_, &len);
	// push the equivalent function in the destination's stack, retrieved from the lookup table
	STACK_CHECK( L2, 0);                                     // L                          // L2
	STACK_GROW( L2, 3); // up to 3 slots are necessary on error
	switch( mode_)
	{
		default: // shouldn't happen, in theory...
		(void) luaL_error( L, "internal error: unknown lookup mode");
		return;

		case eLM_ToKeeper:
		// push a sentinel closure that holds the lookup name as upvalue
		lua_pushlstring( L2, fqn, len);                                                      // "f.q.n"
		lua_pushcclosure( L2, func_lookup_sentinel, 1);                                      // f
		break;

		case eLM_LaneBody:
		case eLM_FromKeeper:
		REGISTRY_GET( L2, LOOKUP_REGKEY);                                                    // {}
		STACK_MID( L2, 1);
		ASSERT_L( lua_istable( L2, -1));
		lua_pushlstring( L2, fqn, len);                                                      // {} "f.q.n"
		lua_rawget( L2, -2);                                                                 // {} f
		// nil means we don't know how to transfer stuff: user should do something
		// anything other than function or table should not happen!
		if( !lua_isfunction( L2, -1) && !lua_istable( L2, -1))
		{
			char const* from, * to;
			lua_getglobal( L, "decoda_name");                    // ... f ... decoda_name
			from = lua_tostring( L, -1);
			lua_pop( L, 1);                                      // ... f ...
			lua_getglobal( L2, "decoda_name");                                                 // {} f decoda_name
			to = lua_tostring( L2, -1);
			lua_pop( L2, 1);                                                                   // {} f
			// when mode_ == eLM_FromKeeper, L is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
			(void) luaL_error(
				(mode_ == eLM_FromKeeper) ? L2 : L
				, "%s%s: function '%s' not found in %s destination transfer database."
				, lua_isnil( L2, -1) ? "" : "INTERNAL ERROR IN "
				, from ? from : "main"
				, fqn
				, to ? to : "main"
			);
			return;
		}
		lua_remove( L2, -2);                                                                 // f
		break;

		/* keep it in case I need it someday, who knows...
		case eLM_RawFunctions:
		{
			int n;
			char const* upname;
			lua_CFunction f = lua_tocfunction( L, i);
			// copy upvalues
			for( n = 0; (upname = lua_getupvalue( L, i, 1 + n)) != NULL; ++ n)
			{
				luaG_inter_move( U, L, L2, 1, mode_);                                            // [up[,up ...]]
			}
			lua_pushcclosure( L2, f, n);                                                       //
		}
		break;
		*/
	}
	STACK_END( L2, 1);
}


/*
 * Copy a function over, which has not been found in the cache.
 * L2 has the cache key for this function at the top of the stack
*/
enum e_vt
{
	VT_NORMAL,
	VT_KEY,
	VT_METATABLE
};
static bool_t inter_copy_one( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum e_vt value_type, LookupMode mode_, char const* upName_);

#if USE_DEBUG_SPEW
static char const* lua_type_names[] =
{
	  "LUA_TNIL"
	, "LUA_TBOOLEAN"
	, "LUA_TLIGHTUSERDATA"
	, "LUA_TNUMBER"
	, "LUA_TSTRING"
	, "LUA_TTABLE"
	, "LUA_TFUNCTION"
	, "LUA_TUSERDATA"
	, "LUA_TTHREAD"
	, "<LUA_NUMTAGS>" // not really a type
	, "LUA_TJITCDATA" // LuaJIT specific
};
static char const* vt_names[] =
{
	"VT_NORMAL"
	, "VT_KEY"
	, "VT_METATABLE"
};
#endif // USE_DEBUG_SPEW

static void copy_func( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, LookupMode mode_, char const* upName_)
{
	int n, needToPush;
	luaL_Buffer b;
	ASSERT_L( L2_cache_i != 0);                                                       // ... {cache} ... p
	STACK_GROW( L, 2);
	STACK_CHECK( L, 0);

	// 'lua_dump()' needs the function at top of stack
	// if already on top of the stack, no need to push again
	needToPush = (i != (uint_t)lua_gettop( L));
	if( needToPush)
	{
		lua_pushvalue( L, i);                                // ... f
	}

	luaL_buffinit( L, &b);
	//
	// "value returned is the error code returned by the last call 
	// to the writer" (and we only return 0)
	// not sure this could ever fail but for memory shortage reasons
	if( lua504_dump( L, buf_writer, &b, 0) != 0)
	{
		luaL_error( L, "internal error: function dump failed.");
	}

	// pushes dumped string on 'L'
	luaL_pushresult( &b);                                  // ... f b

	// if not pushed, no need to pop
	if( needToPush)
	{
		lua_remove( L, -2);                                  // ... b
	}

	// transfer the bytecode, then the upvalues, to create a similar closure
	{
		char const* name = NULL;

		#if LOG_FUNC_INFO
		// "To get information about a function you push it onto the 
		// stack and start the what string with the character '>'."
		//
		{
			lua_Debug ar;
			lua_pushvalue( L, i);                              // ... b f
			// fills 'name' 'namewhat' and 'linedefined', pops function
			lua_getinfo( L, ">nS", &ar);                       // ... b
			name = ar.namewhat;
			fprintf( stderr, INDENT_BEGIN "FNAME: %s @ %d\n", i, s_indent, ar.short_src, ar.linedefined);  // just gives NULL
		}
		#endif // LOG_FUNC_INFO
		{
			size_t sz;
			char const* s = lua_tolstring( L, -1, &sz);        // ... b
			ASSERT_L( s && sz);
			STACK_GROW( L2, 2);
			// Note: Line numbers seem to be taken precisely from the 
			//       original function. 'name' is not used since the chunk
			//       is precompiled (it seems...). 
			//
			// TBD: Can we get the function's original name through, as well?
			//
			if( luaL_loadbuffer( L2, s, sz, name) != 0)                                                // ... {cache} ... p function
			{
				// chunk is precompiled so only LUA_ERRMEM can happen
				// "Otherwise, it pushes an error message"
				//
				STACK_GROW( L, 1);
				luaL_error( L, "%s: %s", upName_, lua_tostring( L2, -1));
			}
			// remove the dumped string
			lua_pop( L, 1);                                    // ...
			// now set the cache as soon as we can.
			// this is necessary if one of the function's upvalues references it indirectly
			// we need to find it in the cache even if it isn't fully transfered yet
			lua_insert( L2, -2);                                                                       // ... {cache} ... function p
			lua_pushvalue( L2, -2);                                                                    // ... {cache} ... function p function
			// cache[p] = function
			lua_rawset( L2, L2_cache_i);                                                               // ... {cache} ... function
		}
		STACK_MID( L, 0);

		/* push over any upvalues; references to this function will come from
		* cache so we don't end up in eternal loop.
		* Lua5.2 and Lua5.3: one of the upvalues is _ENV, which we don't want to copy!
		* instead, the function shall have LUA_RIDX_GLOBALS taken in the destination state!
		*/
		{
			char const* upname;
#if LUA_VERSION_NUM >= 502
			// Starting with Lua 5.2, each Lua function gets its environment as one of its upvalues (named LUA_ENV, aka "_ENV" by default)
			// Generally this is LUA_RIDX_GLOBALS, which we don't want to copy from the source to the destination state...
			// -> if we encounter an upvalue equal to the global table in the source, bind it to the destination's global table
			lua_pushglobaltable( L);                           // ... _G
#endif // LUA_VERSION_NUM
			for( n = 0; (upname = lua_getupvalue( L, i, 1 + n)) != NULL; ++ n)
			{                                                  // ... _G up[n]
				DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "UPNAME[%d]: %s -> " INDENT_END, n, upname));
#if LUA_VERSION_NUM >= 502
				if( lua_rawequal( L, -1, -2)) // is the upvalue equal to the global table?
				{
					DEBUGSPEW_CODE( fprintf( stderr, "pushing destination global scope\n"));
					lua_pushglobaltable( L2);                                                              // ... {cache} ... function <upvalues>
				}
				else
#endif // LUA_VERSION_NUM
				{
					DEBUGSPEW_CODE( fprintf( stderr, "copying value\n"));
					if( !inter_copy_one( U, L2, L2_cache_i, L, lua_gettop( L), VT_NORMAL, mode_, upname))  // ... {cache} ... function <upvalues>
					{
						luaL_error( L, "Cannot copy upvalue type '%s'", luaL_typename( L, -1));
					}
				}
				lua_pop( L, 1);                                  // ... _G
			}
#if LUA_VERSION_NUM >= 502
			lua_pop( L, 1);                                    // ...
#endif // LUA_VERSION_NUM
		}
		// L2: function + 'n' upvalues (>=0)

		STACK_MID( L, 0);

		// Set upvalues (originally set to 'nil' by 'lua_load')
		{
			int func_index = lua_gettop( L2) - n;
			for( ; n > 0; -- n)
			{
				char const* rc = lua_setupvalue( L2, func_index, n);                                     // ... {cache} ... function
				//
				// "assigns the value at the top of the stack to the upvalue and returns its name.
				// It also pops the value from the stack."

				ASSERT_L( rc);      // not having enough slots?
			}
			// once all upvalues have been set we are left
			// with the function at the top of the stack                                               // ... {cache} ... function
		}
	}
	STACK_END( L, 0);
}

/* 
 * Check if we've already copied the same function from 'L', and reuse the old
 * copy.
 *
 * Always pushes a function to 'L2'.
 */
static void copy_cached_func( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, LookupMode mode_, char const* upName_)
{
	FuncSubType funcSubType;
	/*lua_CFunction cfunc =*/ luaG_tocfunction( L, i, &funcSubType); // NULL for LuaJIT-fast && bytecode functions
	if( funcSubType == FST_Bytecode)
	{
		void* const aspointer = (void*)lua_topointer( L, i);
		// TBD: Merge this and same code for tables
		ASSERT_L( L2_cache_i != 0);

		STACK_GROW( L2, 2);

		// L2_cache[id_str]= function
		//
		STACK_CHECK( L2, 0);

		// We don't need to use the from state ('L') in ID since the life span
		// is only for the duration of a copy (both states are locked).
		//

		// push a light userdata uniquely representing the function
		lua_pushlightuserdata( L2, aspointer);                        // ... {cache} ... p

		//fprintf( stderr, "<< ID: %s >>\n", lua_tostring( L2, -1));

		lua_pushvalue( L2, -1);                                       // ... {cache} ... p p
		lua_rawget( L2, L2_cache_i);                                  // ... {cache} ... p function|nil|true

		if( lua_isnil( L2, -1)) // function is unknown
		{
			lua_pop( L2, 1);                                            // ... {cache} ... p

			// Set to 'true' for the duration of creation; need to find self-references
			// via upvalues
			//
			// pushes a copy of the func, stores a reference in the cache
			copy_func( U, L2, L2_cache_i, L, i, mode_, upName_);        // ... {cache} ... function
		}
		else // found function in the cache
		{
			lua_remove( L2, -2);                                        // ... {cache} ... function
		}
		STACK_END( L2, 1);
		ASSERT_L( lua_isfunction( L2, -1));
	}
	else // function is native/LuaJIT: no need to cache
	{
		lookup_native_func( L2, L, i, mode_, upName_);                // ... {cache} ... function
		// if the function was in fact a lookup sentinel, we can either get a function or a table here
		ASSERT_L( lua_isfunction( L2, -1) || lua_istable( L2, -1));
	}
}

static bool_t push_cached_metatable( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum eLookupMode mode_, char const* upName_)
{
	STACK_CHECK( L, 0);
	if( lua_getmetatable( L, i))                                                 // ... mt
	{
		lua_Integer const mt_id = get_mt_id( U, L, -1);    // Unique id for the metatable

		STACK_CHECK( L2, 0);
		STACK_GROW( L2, 4);
		// do we already know this metatable?
		push_registry_subtable( L2, REG_MTID);                                                                   // _R[REG_MTID]
		lua_pushinteger( L2, mt_id);                                                                             // _R[REG_MTID] id
		lua_rawget( L2, -2);                                                                                     // _R[REG_MTID] mt?

		STACK_MID( L2, 2);

		if( lua_isnil( L2, -1))
		{   // L2 did not know the metatable
			lua_pop( L2, 1);                                                                                       // _R[REG_MTID]
			if( inter_copy_one( U, L2, L2_cache_i, L, lua_gettop( L), VT_METATABLE, mode_, upName_))               // _R[REG_MTID] mt
			{
				STACK_MID( L2, 2);
				// mt_id -> metatable
				lua_pushinteger( L2, mt_id);                                                                         // _R[REG_MTID] mt id
				lua_pushvalue( L2, -2);                                                                              // _R[REG_MTID] mt id mt
				lua_rawset( L2, -4);                                                                                 // _R[REG_MTID] mt

				// metatable -> mt_id
				lua_pushvalue( L2, -1);                                                                              // _R[REG_MTID] mt mt
				lua_pushinteger( L2, mt_id);                                                                         // _R[REG_MTID] mt mt id
				lua_rawset( L2, -4);                                                                                 // _R[REG_MTID] mt
			}
			else
			{
				(void) luaL_error( L, "Error copying a metatable");
			}
			STACK_MID( L2, 2);
		}
		lua_remove( L2, -2);                                                                                     // mt

		lua_pop( L, 1);                                                            // ...
		STACK_END( L2, 1);
		STACK_MID( L, 0);
		return TRUE;
	}
	STACK_END( L, 0);
	return FALSE;
}

static void inter_copy_keyvaluepair( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, enum e_vt vt, LookupMode mode_, char const* upName_)
{
	uint_t val_i = lua_gettop( L);
	uint_t key_i = val_i - 1;

	// Only basic key types are copied over; others ignored
	if( inter_copy_one( U, L2, 0 /*key*/, L, key_i, VT_KEY, mode_, upName_))
	{
		char* valPath = (char*) upName_;
		if( U->verboseErrors)
		{
			// for debug purposes, let's try to build a useful name
			if( lua_type( L, key_i) == LUA_TSTRING)
			{
				char const* key = lua_tostring( L, key_i);
				size_t const keyRawLen = lua_rawlen( L, key_i);
				size_t const bufLen = strlen( upName_) + keyRawLen + 2;
				valPath = (char*) alloca( bufLen);
				sprintf( valPath, "%s.%*s", upName_, (int) keyRawLen, key);
				key = NULL;
			}
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
			else if( lua_isinteger( L, key_i))
			{
				lua_Integer key = lua_tointeger( L, key_i);
				valPath = (char*) alloca( strlen( upName_) + 32 + 3);
				sprintf( valPath, "%s[" LUA_INTEGER_FMT "]", upName_, key);
			}
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
			else if( lua_type( L, key_i) == LUA_TNUMBER)
			{
				lua_Number key = lua_tonumber( L, key_i);
				valPath = (char*) alloca( strlen( upName_) + 32 + 3);
				sprintf( valPath, "%s[" LUA_NUMBER_FMT "]", upName_, key);
			}
			else if( lua_type( L, key_i) == LUA_TLIGHTUSERDATA)
			{
				void* key = lua_touserdata( L, key_i);
				valPath = (char*) alloca( strlen( upName_) + 16 + 5);
				sprintf( valPath, "%s[U:%p]", upName_, key);
			}
			else if( lua_type( L, key_i) == LUA_TBOOLEAN)
			{
				int key = lua_toboolean( L, key_i);
				valPath = (char*) alloca( strlen( upName_) + 8);
				sprintf( valPath, "%s[%s]", upName_, key ? "true" : "false");
			}
		}
		/*
		* Contents of metatables are copied with cache checking;
		* important to detect loops.
		*/
		if( inter_copy_one( U, L2, L2_cache_i, L, val_i, VT_NORMAL, mode_, valPath))
		{
			ASSERT_L( lua_istable( L2, -3));
			lua_rawset( L2, -3);    // add to table (pops key & val)
		}
		else
		{
			luaL_error( L, "Unable to copy %s entry '%s' because of value is of type '%s'", (vt == VT_NORMAL) ? "table" : "metatable", valPath, luaL_typename( L, val_i));
		}
	}
}

static bool_t copyclone( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, LookupMode mode_, char const* upName_)
{
	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);

	if( !lua_getmetatable( L, i))                                            // ... mt?
	{
		STACK_MID( L, 0);
		return FALSE;
	}

	lua_getfield( L, -1, "__lanesclone");                                    // ... mt __lanesclone?
	if( lua_isnil( L, -1))
	{
		lua_pop( L, 2);                                                        // ...
		STACK_MID( L, 0);
		return FALSE;
	}

	{
		size_t userdata_size = 0;
		void* const source = lua_touserdata( L, i);
		void* clone = NULL;
		lua_pushvalue( L, -1);                                                 // ... mt __lanesclone __lanesclone
		// call the cloning function with 0 arguments, should return the number of bytes to allocate for the clone
		lua_call( L, 0, 1);                                                    // ... mt __lanesclone size
		STACK_MID( L, 3);
		userdata_size = (size_t) lua_tointeger( L, -1);                        // ... mt __lanesclone size
		lua_pop( L, 1);                                                        // ... mt __lanesclone
		// we need to copy over the uservalues of the userdata as well
		lua_pushnil( L2);                                                                                  // ... nil
		{
			int const clone_i = lua_gettop( L2);
			int uvi = 0;
			while( lua_getiuservalue( L, i, uvi + 1) != LUA_TNONE)               // ... mt __lanesclone uv
			{
				luaG_inter_move( U, L, L2, 1, mode_);                              // ... mt __lanesclone      // ... nil [uv]+
				++ uvi;
			}
			// when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
			lua_pop( L, 1);                                                      // ... mt __lanesclone
				// create the clone userdata with the required number of uservalue slots
			clone = lua_newuserdatauv( L2, userdata_size, uvi);                                              // ... nil [uv]+ u
			lua_replace( L2, clone_i);                                                                       // ... u [uv]+
			// assign uservalues
			while( uvi > 0)
			{
				// this pops the value from the stack
				lua_setiuservalue( L2, clone_i, uvi);                                                          // ... u [uv]+
				-- uvi;
			}
			// when we are done, all uservalues are popped from the stack
			STACK_MID( L2, 1);                                                                               // ... u
		}
		STACK_MID( L, 2);                                                      // ... mt __lanesclone
		// call cloning function in source state to perform the actual memory cloning
		lua_pushlightuserdata( L, clone);                                      // ... mt __lanesclone clone
		lua_pushlightuserdata( L, source);                                     // ... mt __lanesclone source
		lua_call( L, 2, 0);                                                    // ... mt
		STACK_MID( L, 1);
		// copy the metatable in the target state
		if( inter_copy_one( U, L2, L2_cache_i, L, lua_absindex( L, -1), VT_NORMAL, mode_, upName_))        // ... u mt?
		{
			lua_pop( L, 1);                                                      // ...
			STACK_MID( L, 0);
			// when writing to a keeper state, we have here a sentinel function with the metatable's fqn as upvalue
			if( eLM_ToKeeper == mode_)                                                                       // ... u sentinel
			{
				ASSERT_L( lua_tocfunction( L2, -1) == table_lookup_sentinel);
				// we want to create a new closure with a 'clone sentinel' function, where the upvalues are the userdata and the metatable fqn
				lua_getupvalue( L2, -1, 1);                                                                    // ... u sentinel fqn
				lua_remove( L2, -2);                                                                           // ... u fqn
				lua_insert( L2, -2);                                                                           // ... fqn u
				lua_pushcclosure( L2, userdata_clone_sentinel, 2);                                             // ... userdata_clone_sentinel
			}
			else // from keeper or direct, we have the userdata and the metatable
			{
				ASSERT_L( lua_istable( L2, -1));
				lua_setmetatable( L2, -2);                                                                     // ... u
			}
			STACK_MID( L2, 1);
			STACK_MID( L, 0);
			return TRUE;
		}
		else
		{
			(void) luaL_error( L, "Error copying a metatable");
		}
	}

	STACK_END( L2, 1);
	STACK_END( L, 0);
	return FALSE;
}

static bool_t inter_copy_userdata( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum e_vt vt, LookupMode mode_, char const* upName_)
{
	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);
	if( vt == VT_KEY)
	{
		return FALSE;
	}
	// Allow only deep userdata entities to be copied across
	DEBUGSPEW_CODE( fprintf( stderr, "USERDATA\n"));
	if( copydeep( U, L, L2, i, mode_))
	{
		return TRUE;
	}

	STACK_MID( L, 0);
	STACK_MID( L2, 0);

	if( copyclone( U, L2, L2_cache_i, L, i, mode_, upName_))
	{
		STACK_MID( L, 0);
		STACK_MID( L2, 1);
		return TRUE;
	}

	STACK_MID( L, 0);
	STACK_MID( L2, 0);

	// Not a deep or clonable full userdata
	if( U->demoteFullUserdata) // attempt demotion to light userdata
	{
		void* lud = lua_touserdata( L, i);
		lua_pushlightuserdata( L2, lud);
	}
	else // raise an error
	{
		(void) luaL_error( L, "can't copy non-deep full userdata across lanes");
	}

	STACK_END( L2, 1);
	STACK_END( L, 0);
	return TRUE;
}

static bool_t inter_copy_function( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum e_vt vt, LookupMode mode_, char const* upName_)
{
	if( vt == VT_KEY)
	{
		return FALSE;
	}

	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);
	DEBUGSPEW_CODE( fprintf( stderr, "FUNCTION %s\n", upName_));

	if( lua_tocfunction( L, i) == userdata_clone_sentinel) // we are actually copying a clonable full userdata
	{
		// clone the full userdata again
		size_t userdata_size = 0;
		void* source;
		void* clone;
		// this function has 2 upvalues: the fqn of its metatable, and the userdata itself
		lua_getupvalue( L, i, 2);                                                // ... u
		source = lua_touserdata( L, -1);
		lookup_table( L2, L, i, mode_, upName_);                                 // ... u                      // ... mt
		// __lanesclone should always exist because we wouldn't be restoring data from a userdata_clone_sentinel closure to begin with
		lua_getfield( L2, -1, "__lanesclone");                                                                 // ... mt __lanesclone
		lua_pushvalue( L2, -1);                                                                                // ... mt __lanesclone __lanesclone
		// call the cloning function with 0 arguments, should return the number of bytes to allocate for the clone
		lua_call( L2, 0, 1);                                                                                   // ... mt __lanesclone size
		userdata_size = (size_t) lua_tointeger( L2, -1);                                                       // ... mt __lanesclone size
		lua_pop( L2, 1);                                                                                       // ... mt __lanesclone
		lua_pushnil( L2);                                                                                      // ... mt __lanesclone nil
		{
			int const clone_i = lua_gettop( L2);
			int uvi = 0;
			while( lua_getiuservalue( L, -1, uvi + 1) != LUA_TNONE)                // ... u uv
			{
				luaG_inter_move( U, L, L2, 1, mode_);                                // ... u                      // ... mt __lanesclone nil [uv]+
				++ uvi;
			}
			// when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now at the same time as the rest
			lua_pop( L, 2);                                                        // ... u
			STACK_MID( L, 0);                                                      // ...
			// create the clone userdata with the required number of uservalue slots
			clone = lua_newuserdatauv( L2, userdata_size, uvi);                                                  // ... mt __lanesclone nil [uv]+ u
			lua_replace( L2, clone_i);                                                                           // ... mt __lanesclone u [uv]+
			// assign uservalues
			while( uvi > 0)
			{
				// this pops the value from the stack
				lua_setiuservalue( L2, clone_i, uvi);                                                              // ... mt __lanesclone u [uv]+
				-- uvi;
			}
			// when we are done, all uservalues are popped from the stack
			STACK_MID( L2, 3);                                                                                   // ... mt __lanesclone u
		}
		lua_insert( L2, -3);                                                                                   // ... u mt __lanesclone
		lua_pushlightuserdata( L2, clone);                                                                     // ... u mt __lanesclone clone
		lua_pushlightuserdata( L2, source);                                                                    // ... u mt __lanesclone clone source
		lua_call( L2, 2, 0);                                                                                   // ... u mt
		lua_setmetatable( L2, -2);                                                                             // ... u
	}
	else
	{
		DEBUGSPEW_CODE( fprintf( stderr, "FUNCTION %s\n", upName_));
		DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
		STACK_CHECK( L2, 0);
		copy_cached_func( U, L2, L2_cache_i, L, i, mode_, upName_);
		STACK_END( L2, 1);
		DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
	}
	STACK_END( L2, 1);
	STACK_END( L, 0);
	return TRUE;
}

static bool_t inter_copy_table( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum e_vt vt, LookupMode mode_, char const* upName_)
{
	if( vt == VT_KEY)
	{
		return FALSE;
	}

	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);
	DEBUGSPEW_CODE( fprintf( stderr, "TABLE %s\n", upName_));

	/*
		* First, let's try to see if this table is special (aka is it some table that we registered in our lookup databases during module registration?)
		* Note that this table CAN be a module table, but we just didn't register it, in which case we'll send it through the table cloning mechanism
		*/
	if( lookup_table( L2, L, i, mode_, upName_))
	{
		ASSERT_L( lua_istable( L2, -1) || (lua_tocfunction( L2, -1) == table_lookup_sentinel));    // from lookup datables // can also be table_lookup_sentinel if this is a table we know
		return TRUE;
	}

	/* Check if we've already copied the same table from 'L' (during this transmission), and
	* reuse the old copy. This allows table upvalues shared by multiple
	* local functions to point to the same table, also in the target.
	* Also, this takes care of cyclic tables and multiple references
	* to the same subtable.
	*
	* Note: Even metatables need to go through this test; to detect
	*       loops such as those in required module tables (getmetatable(lanes).lanes == lanes)
	*/
	if( push_cached_table( L2, L2_cache_i, L, i))
	{
		ASSERT_L( lua_istable( L2, -1));    // from cache
		return TRUE;
	}
	ASSERT_L( lua_istable( L2, -1));

	STACK_GROW( L, 2);
	STACK_GROW( L2, 2);

	lua_pushnil( L);    // start iteration
	while( lua_next( L, i))
	{
		// need a function to prevent overflowing the stack with verboseErrors-induced alloca()
		inter_copy_keyvaluepair( U, L2, L2_cache_i, L, vt, mode_, upName_);
		lua_pop( L, 1);    // pop value (next round)
	}
	STACK_MID( L, 0);
	STACK_MID( L2, 1);

	// Metatables are expected to be immutable, and copied only once.
	if( push_cached_metatable( U, L2, L2_cache_i, L, i, mode_, upName_))                                   // ... t mt?
	{
		lua_setmetatable( L2, -2);                                                                           // ... t
	}
	STACK_END( L2, 1);
	STACK_END( L, 0);
	return TRUE;
}

/*
* Copies a value from 'L' state (at index 'i') to 'L2' state. Does not remove
* the original value.
*
* NOTE: Both the states must be solely in the current OS thread's possession.
*
* 'i' is an absolute index (no -1, ...)
*
* Returns TRUE if value was pushed, FALSE if its type is non-supported.
*/
static bool_t inter_copy_one( Universe* U, lua_State* L2, uint_t L2_cache_i, lua_State* L, uint_t i, enum e_vt vt, LookupMode mode_, char const* upName_)
{
	bool_t ret = TRUE;
	int val_type = lua_type( L, i);
	static int const pod_mask = (1 << LUA_TNIL) | (1 << LUA_TBOOLEAN) | (1 << LUA_TLIGHTUSERDATA) | (1 << LUA_TNUMBER) | (1 << LUA_TSTRING);
	STACK_GROW( L2, 1);
	STACK_CHECK( L, 0);                                                          // L                          // L2
	STACK_CHECK( L2, 0);                                                         // L                          // L2

	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "inter_copy_one()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%s %s: " INDENT_END, lua_type_names[val_type], vt_names[vt]));

	// Non-POD can be skipped if its metatable contains { __lanesignore = true }
	if( ((1 << val_type) & pod_mask) == 0)
	{
		if( lua_getmetatable( L, i))                                               // ... mt
		{
			lua_getfield( L, -1, "__lanesignore");                                   // ... mt ignore?
			if( lua_isboolean( L, -1) && lua_toboolean( L, -1))
			{
				DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "__lanesignore -> LUA_TNIL\n" INDENT_END));
				val_type = LUA_TNIL;
			}
			lua_pop( L, 2);                                                          // ...
		}
	}
	STACK_MID( L, 0);

	/* Lets push nil to L2 if the object should be ignored */
	switch( val_type)
	{
		/* Basic types allowed both as values, and as table keys */

		case LUA_TBOOLEAN:
		{
			bool_t v = lua_toboolean( L, i);
			DEBUGSPEW_CODE( fprintf( stderr, "%s\n", v ? "true" : "false"));
			lua_pushboolean( L2, v);
		}
		break;

		case LUA_TNUMBER:
		/* LNUM patch support (keeping integer accuracy) */
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
		if( lua_isinteger( L, i))
		{
			lua_Integer v = lua_tointeger( L, i);
			DEBUGSPEW_CODE( fprintf( stderr, LUA_INTEGER_FMT "\n", v));
			lua_pushinteger( L2, v);
			break;
		}
		else
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
		{
			lua_Number v = lua_tonumber( L, i);
			DEBUGSPEW_CODE( fprintf( stderr, LUA_NUMBER_FMT "\n", v));
			lua_pushnumber( L2, v);
		}
		break;

		case LUA_TSTRING:
		{
			size_t len;
			char const* s = lua_tolstring( L, i, &len);
			DEBUGSPEW_CODE( fprintf( stderr, "'%s'\n", s));
			lua_pushlstring( L2, s, len);
		}
		break;

		case LUA_TLIGHTUSERDATA:
		{
			void* p = lua_touserdata( L, i);
			DEBUGSPEW_CODE( fprintf( stderr, "%p\n", p));
			lua_pushlightuserdata( L2, p);
		}
		break;

		/* The following types are not allowed as table keys */

		case LUA_TUSERDATA:
		ret = inter_copy_userdata( U, L2, L2_cache_i, L, i, vt, mode_, upName_);
		break;

		case LUA_TNIL:
		if( vt == VT_KEY)
		{
			ret = FALSE;
			break;
		}
		lua_pushnil( L2);
		break;

		case LUA_TFUNCTION:
		ret = inter_copy_function( U, L2, L2_cache_i, L, i, vt, mode_, upName_);
		break;

		case LUA_TTABLE:
		ret = inter_copy_table( U, L2, L2_cache_i, L, i, vt, mode_, upName_);
		break;

		/* The following types cannot be copied */

		case 10: // LuaJIT CDATA
		case LUA_TTHREAD:
		ret = FALSE;
		break;
	}

	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);

	STACK_END( L2, ret ? 1 : 0);
	STACK_END( L, 0);
	return ret;
}


/*
* Akin to 'lua_xmove' but copies values between _any_ Lua states.
*
* NOTE: Both the states must be solely in the current OS thread's posession.
*
* Note: Parameters are in this order ('L' = from first) to be same as 'lua_xmove'.
*/
int luaG_inter_copy( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_)
{
	uint_t top_L = lua_gettop( L);                                    // ... {}n
	uint_t top_L2 = lua_gettop( L2);                                                               // ...
	uint_t i, j;
	char tmpBuf[16];
	char const* pBuf = U->verboseErrors ? tmpBuf : "?";
	bool_t copyok = TRUE;

	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "luaG_inter_copy()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);

	if( n > top_L)
	{
		// requesting to copy more than is available?
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "nothing to copy()\n" INDENT_END));
		DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
		return -1;
	}

	STACK_CHECK( L2, 0);
	STACK_GROW( L2, n + 1);

	/*
	* Make a cache table for the duration of this copy. Collects tables and
	* function entries, avoiding the same entries to be passed on as multiple
	* copies. ESSENTIAL i.e. for handling upvalue tables in the right manner!
	*/
	lua_newtable( L2);                                                                              // ... cache

	STACK_CHECK( L, 0);
	for( i = top_L - n + 1, j = 1; i <= top_L; ++ i, ++ j)
	{
		if( U->verboseErrors)
		{
			sprintf( tmpBuf, "arg_%d", j);
		}
		copyok = inter_copy_one( U, L2, top_L2 + 1, L, i, VT_NORMAL, mode_, pBuf);                    // ... cache {}n
		if( !copyok)
		{
			break;
		}
	}
	STACK_END( L, 0);

	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);

	if( copyok)
	{
		STACK_MID( L2, n + 1);
		// Remove the cache table. Persistent caching would cause i.e. multiple
		// messages passed in the same table to use the same table also in receiving end.
		lua_remove( L2, top_L2 + 1);
		return 0;
	}

	// error -> pop everything from the target state stack
	lua_settop( L2, top_L2);
	STACK_END( L2, 0);
	return -2;
}


int luaG_inter_move( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_)
{
	int ret = luaG_inter_copy( U, L, L2, n, mode_);
	lua_pop( L, (int) n);
	return ret;
}

int luaG_inter_copy_package( Universe* U, lua_State* L, lua_State* L2, int package_idx_, LookupMode mode_)
{
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "luaG_inter_copy_package()\n" INDENT_END));
	DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
	// package
	STACK_CHECK( L, 0);
	STACK_CHECK( L2, 0);
	package_idx_ = lua_absindex( L, package_idx_);
	if( lua_type( L, package_idx_) != LUA_TTABLE)
	{
		lua_pushfstring( L, "expected package as table, got %s", luaL_typename( L, package_idx_));
		STACK_MID( L, 1);
		// raise the error when copying from lane to lane, else just leave it on the stack to be raised later
		return ( mode_ == eLM_LaneBody) ? lua_error( L) : 1;
	}
	lua_getglobal( L2, "package");
	if( !lua_isnil( L2, -1)) // package library not loaded: do nothing
	{
		int i;
		// package.loaders is renamed package.searchers in Lua 5.2
		// but don't copy it anyway, as the function names change depending on the slot index!
		// users should provide an on_state_create function to setup custom loaders instead
		// don't copy package.preload in keeper states (they don't know how to translate functions)
		char const* entries[] = { "path", "cpath", (mode_ == eLM_LaneBody) ? "preload" : NULL/*, (LUA_VERSION_NUM == 501) ? "loaders" : "searchers"*/, NULL};
		for( i = 0; entries[i]; ++ i)
		{
			DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "package.%s\n" INDENT_END, entries[i]));
			lua_getfield( L, package_idx_, entries[i]);
			if( lua_isnil( L, -1))
			{
				lua_pop( L, 1);
			}
			else
			{
				DEBUGSPEW_CODE( ++ U->debugspew_indent_depth);
				luaG_inter_move( U, L, L2, 1, mode_); // moves the entry to L2
				DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
				lua_setfield( L2, -2, entries[i]); // set package[entries[i]]
			}
		}
	}
	else
	{
		DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "'package' not loaded, nothing to do\n" INDENT_END));
	}
	lua_pop( L2, 1);
	STACK_END( L2, 0);
	STACK_END( L, 0);
	DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
	return 0;
}


/*---=== Serialize require ===---
*/

//---
// [val]= new_require( ... )
//
// Call 'old_require' but only one lane at a time.
//
// Upvalues: [1]: original 'require' function
//
int luaG_new_require( lua_State* L)
{
	int rc, i;
	int args = lua_gettop( L);
	Universe* U = universe_get( L);
	//char const* modname = luaL_checkstring( L, 1);

	STACK_GROW( L, args + 1);
	STACK_CHECK( L, 0);

	lua_pushvalue( L, lua_upvalueindex( 1));
	for( i = 1; i <= args; ++ i)
	{
		lua_pushvalue( L, i);
	}

	// Using 'lua_pcall()' to catch errors; otherwise a failing 'require' would
	// leave us locked, blocking any future 'require' calls from other lanes.
	//
	MUTEX_LOCK( &U->require_cs);
	rc = lua_pcall( L, args, 1 /*retvals*/, 0 /*errfunc*/ );
	MUTEX_UNLOCK( &U->require_cs);

	// the required module (or an error message) is left on the stack as returned value by original require function
	STACK_END( L, 1);

	if( rc != LUA_OK) // LUA_ERRRUN / LUA_ERRMEM ?
	{
		return lua_error( L);   // error message already at [-1]
	}

	return 1;
}

/*
* Serialize calls to 'require', if it exists
*/
void serialize_require( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State* L)
{
	STACK_GROW( L, 1);
	STACK_CHECK( L, 0);
	DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "serializing require()\n" INDENT_END));

	// Check 'require' is there and not already wrapped; if not, do nothing
	//
	lua_getglobal( L, "require");
	if( lua_isfunction( L, -1) && lua_tocfunction( L, -1) != luaG_new_require)
	{
		// [-1]: original 'require' function
		lua_pushcclosure( L, luaG_new_require, 1 /*upvalues*/);
		lua_setglobal( L, "require");
	}
	else
	{
		// [-1]: nil
		lua_pop( L, 1);
	}

	STACK_END( L, 0);
}
