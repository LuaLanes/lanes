/*
* STATE.C
*
* Lua tools to support Lanes.
*/

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
2011-21 benoit Germain <bnt.germain@gmail.com>

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

#include "compat.h"
#include "macros_and_utils.h"
#include "universe.h"
#include "tools.h"
#include "lanes.h"

// ################################################################################################

/*---=== Serialize require ===---
*/

//---
// [val,...]= new_require( ... )
//
// Call 'old_require' but only one lane at a time.
//
// Upvalues: [1]: original 'require' function
//
static int luaG_new_require( lua_State* L)
{
    int rc;
    int const args = lua_gettop( L);                                    // args
    Universe* U = universe_get( L);
    //char const* modname = luaL_checkstring( L, 1);

    STACK_GROW( L, 1);

    lua_pushvalue( L, lua_upvalueindex( 1));                            // args require
    lua_insert( L, 1);                                                  // require args

    // Using 'lua_pcall()' to catch errors; otherwise a failing 'require' would
    // leave us locked, blocking any future 'require' calls from other lanes.
    
    MUTEX_LOCK( &U->require_cs);
    // starting with Lua 5.4, require may return a second optional value, so we need LUA_MULTRET
    rc = lua_pcall( L, args, LUA_MULTRET, 0 /*errfunc*/ ); // err|result(s)
    MUTEX_UNLOCK( &U->require_cs);

    // the required module (or an error message) is left on the stack as returned value by original require function

    if( rc != LUA_OK) // LUA_ERRRUN / LUA_ERRMEM ?
    {
        return lua_error( L);
    }
    // should be 1 for Lua <= 5.3, 1 or 2 starting with Lua 5.4
    return lua_gettop(L);                                           // result(s)
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

// ################################################################################################

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
#if LUAJIT_FLAVOR() != 0 // building against LuaJIT headers, add some LuaJIT-specific libs
//#pragma message( "supporting JIT base libs")
    { LUA_BITLIBNAME, luaopen_bit},
    { LUA_JITLIBNAME, luaopen_jit},
    { LUA_FFILIBNAME, luaopen_ffi},
#endif // LUAJIT_FLAVOR()

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

lua_State* create_state( Universe* U, lua_State* from_)
{
    lua_State* L;
#if LUAJIT_FLAVOR() == 64
    // for some reason, LuaJIT 64 bits does not support creating a state with lua_newstate...
    L = luaL_newstate();
#else // LUAJIT_FLAVOR() == 64
    if( U->provide_allocator != NULL) // we have a function we can call to obtain an allocator
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
        // reuse the allocator provided when the master state was created
        L = lua_newstate( U->protected_allocator.definition.allocF, U->protected_allocator.definition.allocUD);
    }
#endif // LUAJIT_FLAVOR() == 64

    if( L == NULL)
    {
        (void) luaL_error( from_, "luaG_newstate() failed while creating state; out of memory");
    }
    return L;
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

#if 0 && USE_DEBUG_SPEW()
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
#endif // USE_DEBUG_SPEW()

    lua_pop( L, 1);
    STACK_END( L, 0);
    DEBUGSPEW_CODE( -- U->debugspew_indent_depth);
    return L;
}
