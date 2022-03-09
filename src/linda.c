/*
 * LINDA.C   	                    Copyright (c) 2018, Benoit Germain
 *
 * Linda deep userdata.
*/

/*
===============================================================================

Copyright (C) 2018 benoit Germain <bnt.germain@gmail.com>

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

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "threading.h"
#include "compat.h"
#include "tools.h"
#include "universe.h"
#include "keeper.h"
#include "deep.h"
#include "lanes_private.h"

/*
* Actual data is kept within a keeper state, which is hashed by the 's_Linda'
* pointer (which is same to all userdatas pointing to it).
*/
struct s_Linda
{
    DeepPrelude prelude; // Deep userdata MUST start with this header
    SIGNAL_T read_happened;
    SIGNAL_T write_happened;
    Universe* U; // the universe this linda belongs to
    ptrdiff_t group; // a group to control keeper allocation between lindas
    enum e_cancel_request simulate_cancel;
    char name[1];
};
#define LINDA_KEEPER_HASHSEED( linda) (linda->group ? linda->group : (ptrdiff_t)linda)

static void* linda_id( lua_State*, DeepOp);

static inline struct s_Linda* lua_toLinda( lua_State* L, int idx_)
{
    struct s_Linda* linda = (struct s_Linda*) luaG_todeep( L, linda_id, idx_);
    luaL_argcheck( L, linda != NULL, idx_, "expecting a linda object");
    return linda;
}

static void check_key_types( lua_State* L, int start_, int end_)
{
    int i;
    for( i = start_; i <= end_; ++ i)
    {
        int t = lua_type( L, i);
        if( t == LUA_TBOOLEAN || t == LUA_TNUMBER || t == LUA_TSTRING || t == LUA_TLIGHTUSERDATA)
        {
            continue;
        }
        (void) luaL_error( L, "argument #%d: invalid key type (not a boolean, string, number or light userdata)", i);
    }
}

LUAG_FUNC( linda_protected_call)
{
    int rc = LUA_OK;
    struct s_Linda* linda = lua_toLinda( L, 1);

    // acquire the keeper
    Keeper* K = keeper_acquire( linda->U->keepers, LINDA_KEEPER_HASHSEED(linda));
    lua_State* KL = K ? K->L : NULL; // need to do this for 'STACK_CHECK'
    if( KL == NULL) return 0;

    // retrieve the actual function to be called and move it before the arguments
    lua_pushvalue( L, lua_upvalueindex( 1));
    lua_insert( L, 1);
    // do a protected call
    rc = lua_pcall( L, lua_gettop( L) - 1, LUA_MULTRET, 0);

    // release the keeper
    keeper_release( K);

    // if there was an error, forward it
    if( rc != LUA_OK)
    {
        return lua_error( L);
    }
    // return whatever the actual operation provided
    return lua_gettop( L);
}

/*
* bool= linda_send( linda_ud, [timeout_secs=-1,] [linda.null,] key_num|str|bool|lightuserdata, ... )
*
* Send one or more values to a Linda. If there is a limit, all values must fit.
*
* Returns:  'true' if the value was queued
*           'false' for timeout (only happens when the queue size is limited)
*           nil, CANCEL_ERROR if cancelled
*/
LUAG_FUNC( linda_send)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    bool_t ret = FALSE;
    enum e_cancel_request cancel = CANCEL_NONE;
    int pushed;
    time_d timeout = -1.0;
    uint_t key_i = 2; // index of first key, if timeout not there
    bool_t as_nil_sentinel; // if not NULL, send() will silently send a single nil if nothing is provided

    if( lua_type( L, 2) == LUA_TNUMBER) // we don't want to use lua_isnumber() because of autocoercion
    {
        timeout = SIGNAL_TIMEOUT_PREPARE( lua_tonumber( L, 2));
        ++ key_i;
    }
    else if( lua_isnil( L, 2)) // alternate explicit "no timeout" by passing nil before the key
    {
        ++ key_i;
    }

    as_nil_sentinel = equal_unique_key( L, key_i, NIL_SENTINEL);
    if( as_nil_sentinel)
    {
        // the real key to send data to is after the NIL_SENTINEL marker
        ++ key_i;
    }

    // make sure the key is of a valid type
    check_key_types( L, key_i, key_i);

    STACK_GROW( L, 1);

    // make sure there is something to send
    if( (uint_t)lua_gettop( L) == key_i)
    {
        if( as_nil_sentinel)
        {
            // send a single nil if nothing is provided
            push_unique_key( L, NIL_SENTINEL);
        }
        else
        {
            return luaL_error( L, "no data to send");
        }
    }

    // convert nils to some special non-nil sentinel in sent values
    keeper_toggle_nil_sentinels( L, key_i + 1, eLM_ToKeeper);

    {
        bool_t try_again = TRUE;
        Lane* const s = get_lane_from_registry( L);
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));
        lua_State* KL = K ? K->L : NULL; // need to do this for 'STACK_CHECK'
        if( KL == NULL) return 0;
        STACK_CHECK( KL, 0);
        for( ;;)
        {
            if( s != NULL)
            {
                cancel = s->cancel_request;
            }
            cancel = (cancel != CANCEL_NONE) ? cancel : linda->simulate_cancel;
            // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
            if( !try_again || cancel != CANCEL_NONE)
            {
                pushed = 0;
                break;
            }

            STACK_MID( KL, 0);
            pushed = keeper_call( linda->U, KL, KEEPER_API( send), L, linda, key_i);
            if( pushed < 0)
            {
                break;
            }
            ASSERT_L( pushed == 1);

            ret = lua_toboolean( L, -1);
            lua_pop( L, 1);

            if( ret)
            {
                // Wake up ALL waiting threads
                SIGNAL_ALL( &linda->write_happened);
                break;
            }

            // instant timout to bypass the wait syscall
            if( timeout == 0.0)
            {
                break;  /* no wait; instant timeout */
            }

            // storage limit hit, wait until timeout or signalled that we should try again
            {
                enum e_status prev_status = ERROR_ST; // prevent 'might be used uninitialized' warnings
                if( s != NULL)
                {
                    // change status of lane to "waiting"
                    prev_status = s->status; // RUNNING, most likely
                    ASSERT_L( prev_status == RUNNING); // but check, just in case
                    s->status = WAITING;
                    ASSERT_L( s->waiting_on == NULL);
                    s->waiting_on = &linda->read_happened;
                }
                // could not send because no room: wait until some data was read before trying again, or until timeout is reached
                try_again = SIGNAL_WAIT( &linda->read_happened, &K->keeper_cs, timeout);
                if( s != NULL)
                {
                    s->waiting_on = NULL;
                    s->status = prev_status;
                }
            }
        }
        STACK_END( KL, 0);
    }

    if( pushed < 0)
    {
        return luaL_error( L, "tried to copy unsupported types");
    }

    switch( cancel)
    {
        case CANCEL_SOFT:
        // if user wants to soft-cancel, the call returns lanes.cancel_error
        push_unique_key( L, CANCEL_ERROR);
        return 1;

        case CANCEL_HARD:
        // raise an error interrupting execution only in case of hard cancel
        return cancel_error( L); // raises an error and doesn't return

        default:
        lua_pushboolean( L, ret); // true (success) or false (timeout)
        return 1;
    }
}


/*
 * 2 modes of operation
 * [val, key]= linda_receive( linda_ud, [timeout_secs_num=-1], key_num|str|bool|lightuserdata [, ...] )
 * Consumes a single value from the Linda, in any key.
 * Returns: received value (which is consumed from the slot), and the key which had it

 * [val1, ... valCOUNT]= linda_receive( linda_ud, [timeout_secs_num=-1], linda.batched, key_num|str|bool|lightuserdata, min_COUNT[, max_COUNT])
 * Consumes between min_COUNT and max_COUNT values from the linda, from a single key.
 * returns the actual consumed values, or nil if there weren't enough values to consume
 *
 */
#define BATCH_SENTINEL "270e6c9d-280f-4983-8fee-a7ecdda01475"
LUAG_FUNC( linda_receive)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    int pushed, expected_pushed_min, expected_pushed_max;
    enum e_cancel_request cancel = CANCEL_NONE;
    keeper_api_t keeper_receive;
    
    time_d timeout = -1.0;
    uint_t key_i = 2;

    if( lua_type( L, 2) == LUA_TNUMBER) // we don't want to use lua_isnumber() because of autocoercion
    {
        timeout = SIGNAL_TIMEOUT_PREPARE( lua_tonumber( L, 2));
        ++ key_i;
    }
    else if( lua_isnil( L, 2)) // alternate explicit "no timeout" by passing nil before the key
    {
        ++ key_i;
    }

    // are we in batched mode?
    {
        int is_batched;
        lua_pushliteral( L, BATCH_SENTINEL);
        is_batched = lua501_equal( L, key_i, -1);
        lua_pop( L, 1);
        if( is_batched)
        {
            // no need to pass linda.batched in the keeper state
            ++ key_i;
            // make sure the keys are of a valid type
            check_key_types( L, key_i, key_i);
            // receive multiple values from a single slot
            keeper_receive = KEEPER_API( receive_batched);
            // we expect a user-defined amount of return value
            expected_pushed_min = (int)luaL_checkinteger( L, key_i + 1);
            expected_pushed_max = (int)luaL_optinteger( L, key_i + 2, expected_pushed_min);
            // don't forget to count the key in addition to the values
            ++ expected_pushed_min;
            ++ expected_pushed_max;
            if( expected_pushed_min > expected_pushed_max)
            {
                return luaL_error( L, "batched min/max error");
            }
        }
        else
        {
            // make sure the keys are of a valid type
            check_key_types( L, key_i, lua_gettop( L));
            // receive a single value, checking multiple slots
            keeper_receive = KEEPER_API( receive);
            // we expect a single (value, key) pair of returned values
            expected_pushed_min = expected_pushed_max = 2;
        }
    }

    {
        bool_t try_again = TRUE;
        Lane* const s = get_lane_from_registry( L);
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));
        if( K == NULL) return 0;
        for( ;;)
        {
            if( s != NULL)
            {
                cancel = s->cancel_request;
            }
            cancel = (cancel != CANCEL_NONE) ? cancel : linda->simulate_cancel;
            // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
            if( !try_again || cancel != CANCEL_NONE)
            {
                pushed = 0;
                break;
            }

            // all arguments of receive() but the first are passed to the keeper's receive function
            pushed = keeper_call( linda->U, K->L, keeper_receive, L, linda, key_i);
            if( pushed < 0)
            {
                break;
            }
            if( pushed > 0)
            {
                ASSERT_L( pushed >= expected_pushed_min && pushed <= expected_pushed_max);
                // replace sentinels with real nils
                keeper_toggle_nil_sentinels( L, lua_gettop( L) - pushed, eLM_FromKeeper);
                // To be done from within the 'K' locking area
                //
                SIGNAL_ALL( &linda->read_happened);
                break;
            }

            if( timeout == 0.0)
            {
                break;  /* instant timeout */
            }

            // nothing received, wait until timeout or signalled that we should try again
            {
                enum e_status prev_status = ERROR_ST; // prevent 'might be used uninitialized' warnings
                if( s != NULL)
                {
                    // change status of lane to "waiting"
                    prev_status = s->status; // RUNNING, most likely
                    ASSERT_L( prev_status == RUNNING); // but check, just in case
                    s->status = WAITING;
                    ASSERT_L( s->waiting_on == NULL);
                    s->waiting_on = &linda->write_happened;
                }
                // not enough data to read: wakeup when data was sent, or when timeout is reached
                try_again = SIGNAL_WAIT( &linda->write_happened, &K->keeper_cs, timeout);
                if( s != NULL)
                {
                    s->waiting_on = NULL;
                    s->status = prev_status;
                }
            }
        }
    }

    if( pushed < 0)
    {
        return luaL_error( L, "tried to copy unsupported types");
    }

    switch( cancel)
    {
        case CANCEL_SOFT:
        // if user wants to soft-cancel, the call returns CANCEL_ERROR
        push_unique_key( L, CANCEL_ERROR);
        return 1;

        case CANCEL_HARD:
        // raise an error interrupting execution only in case of hard cancel
        return cancel_error( L); // raises an error and doesn't return

        default:
        return pushed;
    }
}


/*
* [true|lanes.cancel_error] = linda_set( linda_ud, key_num|str|bool|lightuserdata [, value [, ...]])
*
* Set one or more value to Linda.
* TODO: what do we do if we set to non-nil and limit is 0?
*
* Existing slot value is replaced, and possible queued entries removed.
*/
LUAG_FUNC( linda_set)
{
    struct s_Linda* const linda = lua_toLinda( L, 1);
    int pushed;
    bool_t has_value = lua_gettop( L) > 2;

    // make sure the key is of a valid type (throws an error if not the case)
    check_key_types( L, 2, 2);

    {
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));

        if( linda->simulate_cancel == CANCEL_NONE)
        {
            if( has_value)
            {
                // convert nils to some special non-nil sentinel in sent values
                keeper_toggle_nil_sentinels( L, 3, eLM_ToKeeper);
            }
            pushed = keeper_call( linda->U, K->L, KEEPER_API( set), L, linda, 2);
            if( pushed >= 0) // no error?
            {
                ASSERT_L( pushed == 0 || pushed == 1);

                if( has_value)
                {
                    // we put some data in the slot, tell readers that they should wake
                    SIGNAL_ALL( &linda->write_happened); // To be done from within the 'K' locking area
                }
                if( pushed == 1)
                {
                    // the key was full, but it is no longer the case, tell writers they should wake
                    ASSERT_L( lua_type( L, -1) == LUA_TBOOLEAN && lua_toboolean( L, -1) == 1);
                    SIGNAL_ALL( &linda->read_happened); // To be done from within the 'K' locking area
                }
            }
        }
        else // linda is cancelled
        {
            // do nothing and return lanes.cancel_error
            push_unique_key( L, CANCEL_ERROR);
            pushed = 1;
        }
    }

    // must trigger any error after keeper state has been released
    return (pushed < 0) ? luaL_error( L, "tried to copy unsupported types") : pushed;
}


/*
 * [val] = linda_count( linda_ud, [key [, ...]])
 *
 * Get a count of the pending elements in the specified keys
 */
LUAG_FUNC( linda_count)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    int pushed;

    // make sure the keys are of a valid type
    check_key_types( L, 2, lua_gettop( L));

    {
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));
        pushed = keeper_call( linda->U, K->L, KEEPER_API( count), L, linda, 2);
        if( pushed < 0)
        {
            return luaL_error( L, "tried to count an invalid key");
        }
    }
    return pushed;
}


/*
* [val [, ...]] = linda_get( linda_ud, key_num|str|bool|lightuserdata [, count = 1])
*
* Get one or more values from Linda.
*/
LUAG_FUNC( linda_get)
{
    struct s_Linda* const linda = lua_toLinda( L, 1);
    int pushed;
    lua_Integer count = luaL_optinteger( L, 3, 1);
    luaL_argcheck( L, count >= 1, 3, "count should be >= 1");
    luaL_argcheck( L, lua_gettop( L) <= 3, 4, "too many arguments");

    // make sure the key is of a valid type (throws an error if not the case)
    check_key_types( L, 2, 2);
    {
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));

        if( linda->simulate_cancel == CANCEL_NONE)
        {
            pushed = keeper_call( linda->U, K->L, KEEPER_API( get), L, linda, 2);
            if( pushed > 0)
            {
                keeper_toggle_nil_sentinels( L, lua_gettop( L) - pushed, eLM_FromKeeper);
            }
        }
        else // linda is cancelled
        {
            // do nothing and return lanes.cancel_error
            push_unique_key( L, CANCEL_ERROR);
            pushed = 1;
        }
        // an error can be raised if we attempt to read an unregistered function
        if( pushed < 0)
        {
            return luaL_error( L, "tried to copy unsupported types");
        }
    }

    return pushed;
}


/*
* [true] = linda_limit( linda_ud, key_num|str|bool|lightuserdata, int)
*
* Set limit to 1 Linda keys.
* Optionally wake threads waiting to write on the linda, in case the limit enables them to do so
*/
LUAG_FUNC( linda_limit)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    int pushed;

    // make sure we got 3 arguments: the linda, a key and a limit
    luaL_argcheck( L, lua_gettop( L) == 3, 2, "wrong number of arguments");
    // make sure we got a numeric limit
    luaL_checknumber( L, 3);
    // make sure the key is of a valid type
    check_key_types( L, 2, 2);

    {
        Keeper* K = which_keeper( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));

        if( linda->simulate_cancel == CANCEL_NONE)
        {
            pushed = keeper_call( linda->U, K->L, KEEPER_API( limit), L, linda, 2);
            ASSERT_L( pushed == 0 || pushed == 1); // no error, optional boolean value saying if we should wake blocked writer threads
            if( pushed == 1)
            {
                ASSERT_L( lua_type( L, -1) == LUA_TBOOLEAN && lua_toboolean( L, -1) == 1);
                SIGNAL_ALL( &linda->read_happened); // To be done from within the 'K' locking area
            }
        }
        else // linda is cancelled
        {
            // do nothing and return lanes.cancel_error
            push_unique_key( L, CANCEL_ERROR);
            pushed = 1;
        }
    }
    // propagate pushed boolean if any
    return pushed;
}


/*
* (void) = linda_cancel( linda_ud, "read"|"write"|"both"|"none")
*
* Signal linda so that waiting threads wake up as if their own lane was cancelled
*/
LUAG_FUNC( linda_cancel)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    char const* who = luaL_optstring( L, 2, "both");

    // make sure we got 3 arguments: the linda, a key and a limit
    luaL_argcheck( L, lua_gettop( L) <= 2, 2, "wrong number of arguments");

    linda->simulate_cancel = CANCEL_SOFT;
    if( strcmp( who, "both") == 0) // tell everyone writers to wake up
    {
        SIGNAL_ALL( &linda->write_happened);
        SIGNAL_ALL( &linda->read_happened);
    }
    else if( strcmp( who, "none") == 0) // reset flag
    {
        linda->simulate_cancel = CANCEL_NONE;
    }
    else if( strcmp( who, "read") == 0) // tell blocked readers to wake up
    {
        SIGNAL_ALL( &linda->write_happened);
    }
    else if( strcmp( who, "write") == 0) // tell blocked writers to wake up
    {
        SIGNAL_ALL( &linda->read_happened);
    }
    else
    {
        return luaL_error( L, "unknown wake hint '%s'", who);
    }
    return 0;
}


/*
* lightuserdata= linda_deep( linda_ud )
*
* Return the 'deep' userdata pointer, identifying the Linda.
*
* This is needed for using Lindas as key indices (timer system needs it);
* separately created proxies of the same underlying deep object will have
* different userdata and won't be known to be essentially the same deep one
* without this.
*/
LUAG_FUNC( linda_deep)
{
    struct s_Linda* linda= lua_toLinda( L, 1);
    lua_pushlightuserdata( L, linda); // just the address
    return 1;
}


/*
* string = linda:__tostring( linda_ud)
*
* Return the stringification of a linda
*
* Useful for concatenation or debugging purposes
*/

static int linda_tostring( lua_State* L, int idx_, bool_t opt_)
{
    struct s_Linda* linda = (struct s_Linda*) luaG_todeep( L, linda_id, idx_);
    if( !opt_)
    {
        luaL_argcheck( L, linda, idx_, "expecting a linda object");
    }
    if( linda != NULL)
    {
        char text[128];
        int len;
        if( linda->name[0])
            len = sprintf( text, "Linda: %.*s", (int)sizeof(text) - 8, linda->name);
        else
            len = sprintf( text, "Linda: %p", linda);
        lua_pushlstring( L, text, len);
        return 1;
    }
    return 0;
}

LUAG_FUNC( linda_tostring)
{
    return linda_tostring( L, 1, FALSE);
}


/*
* string = linda:__concat( a, b)
*
* Return the concatenation of a pair of items, one of them being a linda
*
* Useful for concatenation or debugging purposes
*/
LUAG_FUNC( linda_concat)
{                                   // linda1? linda2?
    bool_t atLeastOneLinda = FALSE;
    // Lua semantics enforce that one of the 2 arguments is a Linda, but not necessarily both.
    if( linda_tostring( L, 1, TRUE))
    {
        atLeastOneLinda = TRUE;
        lua_replace( L, 1);
    }
    if( linda_tostring( L, 2, TRUE))
    {
        atLeastOneLinda = TRUE;
        lua_replace( L, 2);
    }
    if( !atLeastOneLinda) // should not be possible
    {
        return luaL_error( L, "internal error: linda_concat called on non-Linda");
    }
    lua_concat( L, 2);
    return 1;
}

/*
 * table = linda:dump()
 * return a table listing all pending data inside the linda
 */
LUAG_FUNC( linda_dump)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    ASSERT_L( linda->U == universe_get( L));
    return keeper_push_linda_storage( linda->U, L, linda, LINDA_KEEPER_HASHSEED( linda));
}

/*
 * table = linda:dump()
 * return a table listing all pending data inside the linda
 */
LUAG_FUNC( linda_towatch)
{
    struct s_Linda* linda = lua_toLinda( L, 1);
    int pushed;
    ASSERT_L( linda->U == universe_get( L));
    pushed = keeper_push_linda_storage( linda->U, L, linda, LINDA_KEEPER_HASHSEED( linda));
    if( pushed == 0)
    {
        // if the linda is empty, don't return nil
        pushed = linda_tostring( L, 1, FALSE);
    }
    return pushed;
}

/*
* Identity function of a shared userdata object.
* 
*   lightuserdata= linda_id( "new" [, ...] )
*   = linda_id( "delete", lightuserdata )
*
* Creation and cleanup of actual 'deep' objects. 'luaG_...' will wrap them into
* regular userdata proxies, per each state using the deep data.
*
*   tbl= linda_id( "metatable" )
*
* Returns a metatable for the proxy objects ('__gc' method not needed; will
* be added by 'luaG_...')
*
*   string= linda_id( "module")
*
* Returns the name of the module that a state should require
* in order to keep a handle on the shared library that exported the idfunc
*
*   = linda_id( str, ... )
*
* For any other strings, the ID function must not react at all. This allows
* future extensions of the system. 
*/
static void* linda_id( lua_State* L, DeepOp op_)
{
    switch( op_)
    {
        case eDO_new:
        {
            struct s_Linda* s;
            size_t name_len = 0;
            char const* linda_name = NULL;
            unsigned long linda_group = 0;
            // should have a string and/or a number of the stack as parameters (name and group)
            switch( lua_gettop( L))
            {
                default: // 0
                break;

                case 1: // 1 parameter, either a name or a group
                if( lua_type( L, -1) == LUA_TSTRING)
                {
                    linda_name = lua_tolstring( L, -1, &name_len);
                }
                else
                {
                    linda_group = (unsigned long) lua_tointeger( L, -1);
                }
                break;

                case 2: // 2 parameters, a name and group, in that order
                linda_name = lua_tolstring( L, -2, &name_len);
                linda_group = (unsigned long) lua_tointeger( L, -1);
                break;
            }

            /* The deep data is allocated separately of Lua stack; we might no
            * longer be around when last reference to it is being released.
            * One can use any memory allocation scheme.
            * just don't use L's allocF because we don't know which state will get the honor of GCing the linda
            */
            // don't hijack the state allocator when running LuaJIT because it looks like LuaJIT does not expect it and might invalidate the memory unexpectedly
#if USE_LUA_STATE_ALLOCATOR()
            {
                Universe* const U = universe_get(L);
                AllocatorDefinition* const allocD = &U->protected_allocator.definition;

                s = (struct s_Linda*)allocD->allocF(allocD->allocUD, NULL, 0, sizeof(struct s_Linda) + name_len); // terminating 0 is already included
            }
#else // USE_LUA_STATE_ALLOCATOR()
            s = (struct s_Linda*)malloc(sizeof(struct s_Linda) + name_len); // terminating 0 is already included
#endif // USE_LUA_STATE_ALLOCATOR()
            if( s)
            {
                s->prelude.magic.value = DEEP_VERSION.value;
                SIGNAL_INIT( &s->read_happened);
                SIGNAL_INIT( &s->write_happened);
                s->U = universe_get( L);
                s->simulate_cancel = CANCEL_NONE;
                s->group = linda_group << KEEPER_MAGIC_SHIFT;
                s->name[0] = 0;
                memcpy( s->name, linda_name, name_len ? name_len + 1 : 0);
            }
            return s;
        }

        case eDO_delete:
        {
            Keeper* K;
            struct s_Linda* linda = lua_touserdata( L, 1);
            ASSERT_L( linda);

            // Clean associated structures in the keeper state.
            K = keeper_acquire( linda->U->keepers, LINDA_KEEPER_HASHSEED( linda));
            if( K && K->L) // can be NULL if this happens during main state shutdown (lanes is GC'ed -> no keepers -> no need to cleanup)
            {
                // hopefully this won't ever raise an error as we would jump to the closest pcall site while forgetting to release the keeper mutex...
                keeper_call( linda->U, K->L, KEEPER_API( clear), L, linda, 0);
            }
            keeper_release( K);

            // There aren't any lanes waiting on these lindas, since all proxies have been gc'ed. Right?
            SIGNAL_FREE( &linda->read_happened);
            SIGNAL_FREE( &linda->write_happened);
            // don't hijack the state allocator when running LuaJIT because it looks like LuaJIT does not expect it and might invalidate the memory unexpectedly
#if USE_LUA_STATE_ALLOCATOR()
            {
                Universe* const U = universe_get(L);
                AllocatorDefinition* const allocD = &U->protected_allocator.definition;

                allocD->allocF(allocD->allocUD, linda, sizeof(struct s_Linda) + strlen(linda->name), 0);
            }
#else // USE_LUA_STATE_ALLOCATOR()
            free(linda);
#endif // USE_LUA_STATE_ALLOCATOR()
            return NULL;
        }

        case eDO_metatable:
        {

            STACK_CHECK( L, 0);
            lua_newtable( L);
            // metatable is its own index
            lua_pushvalue( L, -1);
            lua_setfield( L, -2, "__index");

            // protect metatable from external access
            lua_pushliteral( L, "Linda");
            lua_setfield( L, -2, "__metatable");

            lua_pushcfunction( L, LG_linda_tostring);
            lua_setfield( L, -2, "__tostring");

            // Decoda __towatch support
            lua_pushcfunction( L, LG_linda_towatch);
            lua_setfield( L, -2, "__towatch");

            lua_pushcfunction( L, LG_linda_concat);
            lua_setfield( L, -2, "__concat");

            // protected calls, to ensure associated keeper is always released even in case of error
            // all function are the protected call wrapper, where the actual operation is provided as upvalue
            // note that this kind of thing can break function lookup as we use the function pointer here and there

            lua_pushcfunction( L, LG_linda_send);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "send");

            lua_pushcfunction( L, LG_linda_receive);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "receive");

            lua_pushcfunction( L, LG_linda_limit);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "limit");

            lua_pushcfunction( L, LG_linda_set);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "set");

            lua_pushcfunction( L, LG_linda_count);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "count");

            lua_pushcfunction( L, LG_linda_get);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "get");

            lua_pushcfunction( L, LG_linda_cancel);
            lua_setfield( L, -2, "cancel");

            lua_pushcfunction( L, LG_linda_deep);
            lua_setfield( L, -2, "deep");

            lua_pushcfunction( L, LG_linda_dump);
            lua_pushcclosure( L, LG_linda_protected_call, 1);
            lua_setfield( L, -2, "dump");

            // some constants
            lua_pushliteral( L, BATCH_SENTINEL);
            lua_setfield( L, -2, "batched");

            push_unique_key( L, NIL_SENTINEL);
            lua_setfield( L, -2, "null");

            STACK_END( L, 1);
            return NULL;
        }

        case eDO_module:
        // linda is a special case because we know lanes must be loaded from the main lua state
        // to be able to ever get here, so we know it will remain loaded as long a the main state is around
        // in other words, forever.
        default:
        {
            return NULL;
        }
    }
}

/*
 * ud = lanes.linda( [name[,group]])
 *
 * returns a linda object, or raises an error if creation failed
 */
LUAG_FUNC( linda)
{
    int const top = lua_gettop( L);
    luaL_argcheck( L, top <= 2, top, "too many arguments");
    if( top == 1)
    {
        int const t = lua_type( L, 1);
        luaL_argcheck( L, t == LUA_TSTRING || t == LUA_TNUMBER, 1, "wrong parameter (should be a string or a number)");
    }
    else if( top == 2)
    {
        luaL_checktype( L, 1, LUA_TSTRING);
        luaL_checktype( L, 2, LUA_TNUMBER);
    }
    return luaG_newdeepuserdata( L, linda_id, 0);
}
