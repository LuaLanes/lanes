/*
 * MACROS_AND_UTILS.H
 */
#ifndef MACROS_AND_UTILS_H
#define MACROS_AND_UTILS_H

#include "lua.h"
#include "lualib.h"

 // M$ compiler doesn't support 'inline' keyword in C files...
#if defined( _MSC_VER)
#define inline __inline
#endif

#define USE_DEBUG_SPEW() 0
#if USE_DEBUG_SPEW()
extern char const* debugspew_indent;
#define INDENT_BEGIN "%.*s "
#define INDENT_END , (U ? U->debugspew_indent_depth : 0), debugspew_indent
#define DEBUGSPEW_CODE(_code) _code
#define DEBUGSPEW_PARAM_COMMA( param_) param_,
#define DEBUGSPEW_COMMA_PARAM( param_) , param_
#else // USE_DEBUG_SPEW()
#define DEBUGSPEW_CODE(_code)
#define DEBUGSPEW_PARAM_COMMA( param_)
#define DEBUGSPEW_COMMA_PARAM( param_)
#endif // USE_DEBUG_SPEW()

#ifdef NDEBUG

#define _ASSERT_L(lua,c)     //nothing
#define STACK_CHECK(L,o)     //nothing
#define STACK_CHECK_ABS(L,o) //nothing
#define STACK_MID(L,c)       //nothing
#define STACK_END(L,c)       //nothing
#define STACK_DUMP(L)        //nothing

#else // NDEBUG

#define _ASSERT_L( L, cond_) if( (cond_) == 0) { (void) luaL_error( L, "ASSERT failed: %s:%d '%s'", __FILE__, __LINE__, #cond_);}

#define STACK_CHECK( L, offset_) \
    { \
        int const L##_delta = offset_; \
        if( (L##_delta < 0) || (lua_gettop( L) < L##_delta)) \
        { \
            assert( FALSE); \
            (void) luaL_error( L, "STACK INIT ASSERT failed (%d not %d): %s:%d", lua_gettop( L), L##_delta, __FILE__, __LINE__); \
        } \
        int const L##_oldtop = lua_gettop( L) - L##_delta

#define STACK_CHECK_ABS( L, offset_) \
    { \
        int const L##_pos = offset_; \
        if( lua_gettop( L) < L##_pos) \
        { \
            assert( FALSE); \
            (void) luaL_error( L, "STACK INIT ASSERT failed (%d not %d): %s:%d", lua_gettop( L), L##_pos, __FILE__, __LINE__); \
        } \
        int const L##_oldtop = 0

#define STACK_MID( L, change) \
        do if( change != LUA_MULTRET) \
        { \
            int stack_check_a = lua_gettop( L) - L##_oldtop; \
            int stack_check_b = (change); \
            if( stack_check_a != stack_check_b) \
            { \
                assert( FALSE); \
                luaL_error( L, "STACK ASSERT failed (%d not %d): %s:%d", stack_check_a, stack_check_b, __FILE__, __LINE__); \
            } \
        } while( 0)

#define STACK_END( L, change) \
        STACK_MID( L, change); \
    }

#define STACK_DUMP( L)    luaG_dump( L)

#endif // NDEBUG

#define ASSERT_L(c) _ASSERT_L(L,c)

#define STACK_GROW( L, n) do { if (!lua_checkstack(L,(int)(n))) luaL_error( L, "Cannot grow stack!" ); } while( 0)

// non-string keyed registry access
#define REGISTRY_SET( L, key_, value_) \
{ \
    push_unique_key( L, key_); \
    value_; \
    lua_rawset( L, LUA_REGISTRYINDEX); \
}

#define REGISTRY_GET( L, key_) \
{ \
    push_unique_key( L, key_); \
    lua_rawget( L, LUA_REGISTRYINDEX); \
}

#define LUAG_FUNC( func_name) int LG_##func_name( lua_State* L)

// after all, it looks like we can use the state allocator for our own usage when running LuaJIT, as long as we mutex-protect it
#define USE_LUA_STATE_ALLOCATOR() 1 // (LUAJIT_FLAVOR()==0)

#endif // MACROS_AND_UTILS_H
