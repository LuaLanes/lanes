/*
 --
 -- KEEPER.CPP
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

 Copyright (C) 2011-2024 Benoit Germain <bnt.germain@gmail.com>

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
#include "keeper.h"

#include "compat.h"
#include "state.h"
#include "tools.h"
#include "uniquekey.h"
#include "universe.h"

#include <algorithm>
#include <cassert>

// ###################################################################################
// Keeper implementation
// ###################################################################################

class keeper_fifo
{
    public:

    int first{ 1 };
    int count{ 0 };
    int limit{ -1 };

    // a fifo full userdata has one uservalue, the table that holds the actual fifo contents
    static void* operator new([[maybe_unused]] size_t size_, lua_State* L) noexcept { return lua_newuserdatauv<keeper_fifo>(L, 1); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* p_, lua_State* L) { ASSERT_L(!"should never be called") };

    static keeper_fifo* getPtr(lua_State* L, int idx_)
    {
        return lua_tofulluserdata<keeper_fifo>(L, idx_);
    }
};

static constexpr int CONTENTS_TABLE{ 1 };

// ##################################################################################################

// replaces the fifo ud by its uservalue on the stack
static keeper_fifo* prepare_fifo_access(lua_State* L, int idx_)
{
    keeper_fifo* const fifo{ keeper_fifo::getPtr(L, idx_) };
    if (fifo != nullptr)
    {
        idx_ = lua_absindex(L, idx_);
        STACK_GROW(L, 1);
        // we can replace the fifo userdata in the stack without fear of it being GCed, there are other references around
        lua_getiuservalue(L, idx_, CONTENTS_TABLE);
        lua_replace(L, idx_);
    }
    return fifo;
}

// ##################################################################################################

// in: nothing
// out: { first = 1, count = 0, limit = -1}
static keeper_fifo* fifo_new(lua_State* L)
{
    STACK_GROW(L, 2);
    STACK_CHECK_START_REL(L, 0);
    keeper_fifo* const fifo{ new (L) keeper_fifo{} };
    STACK_CHECK(L, 1);
    lua_newtable(L);
    lua_setiuservalue(L, -2, CONTENTS_TABLE);
    STACK_CHECK(L, 1);
    return fifo;
}

// ##################################################################################################

// in: expect fifo ... on top of the stack
// out: nothing, removes all pushed values from the stack
static void fifo_push(lua_State* L, keeper_fifo* fifo_, int count_)
{
    int const idx{ lua_gettop(L) - count_ };
    int const start{ fifo_->first + fifo_->count - 1 };
    // pop all additional arguments, storing them in the fifo
    for (int i = count_; i >= 1; --i)
    {
        // store in the fifo the value at the top of the stack at the specified index, popping it from the stack
        lua_rawseti(L, idx, start + i);
    }
    fifo_->count += count_;
}

// ##################################################################################################

// in: fifo
// out: ...|nothing
// expects exactly 1 value on the stack!
// currently only called with a count of 1, but this may change in the future
// function assumes that there is enough data in the fifo to satisfy the request
static void fifo_peek(lua_State* L, keeper_fifo* fifo_, int count_)
{
    STACK_GROW(L, count_);
    for (int i = 0; i < count_; ++i)
    {
        lua_rawgeti(L, 1, (fifo_->first + i));
    }
}

// ##################################################################################################

// in: fifo
// out: remove the fifo from the stack, push as many items as required on the stack (function assumes they exist in sufficient number)
static void fifo_pop( lua_State* L, keeper_fifo* fifo_, int count_)
{
    ASSERT_L(lua_istable(L, -1));
    int const fifo_idx{ lua_gettop(L) };       // ... fifotbl
    // each iteration pushes a value on the stack!
    STACK_GROW(L, count_ + 2);
    // skip first item, we will push it last
    for (int i = 1; i < count_; ++i)
    {
        int const at{ fifo_->first + i };
        // push item on the stack
        lua_rawgeti(L, fifo_idx, at);          // ... fifotbl val
        // remove item from the fifo
        lua_pushnil(L);                        // ... fifotbl val nil
        lua_rawseti(L, fifo_idx, at);          // ... fifotbl val
    }
    // now process first item
    {
        int const at{ fifo_->first };
        lua_rawgeti(L, fifo_idx, at);          // ... fifotbl vals val
        lua_pushnil(L);                        // ... fifotbl vals val nil
        lua_rawseti(L, fifo_idx, at);          // ... fifotbl vals val
        lua_replace(L, fifo_idx);              // ... vals
    }

    // avoid ever-growing indexes by resetting each time we detect the fifo is empty
    {
        int const new_count{ fifo_->count - count_ };
        fifo_->first = (new_count == 0) ? 1 : (fifo_->first + count_);
        fifo_->count = new_count;
    }
}

// ##################################################################################################

// in: linda_ud expected at stack slot idx
// out: fifos[ud]
// crc64/we of string "FIFOS_KEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey FIFOS_KEY{ 0xdce50bbc351cd465ull };
static void push_table(lua_State* L, int idx_)
{
    STACK_GROW(L, 5);
    STACK_CHECK_START_REL(L, 0);
    idx_ = lua_absindex(L, idx_);
    FIFOS_KEY.pushValue(L);                    // ud fifos
    lua_pushvalue(L, idx_);                    // ud fifos ud
    lua_rawget(L, -2);                         // ud fifos fifos[ud]
    STACK_CHECK(L, 2);
    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);                         // ud fifos
        // add a new fifos table for this linda
        lua_newtable(L);                       // ud fifos fifos[ud]
        lua_pushvalue(L, idx_);                // ud fifos fifos[ud] ud
        lua_pushvalue(L, -2);                  // ud fifos fifos[ud] ud fifos[ud]
        lua_rawset(L, -4);                     // ud fifos fifos[ud]
    }
    lua_remove(L, -2);                         // ud fifos[ud]
    STACK_CHECK(L, 1);
}

// ##################################################################################################

int keeper_push_linda_storage(Universe* U, Dest L, void* ptr_, uintptr_t magic_)
{
    Keeper* const K{ which_keeper(U->keepers, magic_) };
    Source const KL{ K ? K->L : nullptr };
    if (KL == nullptr)
        return 0;
    STACK_GROW(KL, 4);
    STACK_CHECK_START_REL(KL, 0);
    FIFOS_KEY.pushValue(KL);                                      // fifos
    lua_pushlightuserdata(KL, ptr_);                              // fifos ud
    lua_rawget(KL, -2);                                           // fifos storage
    lua_remove(KL, -2);                                           // storage
    if (!lua_istable(KL, -1))
    {
        lua_pop(KL, 1);                                           //
        STACK_CHECK(KL, 0);
        return 0;
    }
    // move data from keeper to destination state                    KEEPER                       MAIN
    lua_pushnil(KL);                                              // storage nil
    STACK_GROW(L, 5);
    STACK_CHECK_START_REL(L, 0);
    lua_newtable(L);                                                                           // out
    while (lua_next(KL, -2))                                      // storage key fifo
    {
        keeper_fifo* fifo = prepare_fifo_access(KL, -1);          // storage key fifotbl
        lua_pushvalue(KL, -2);                                    // storage key fifotbl key
        luaG_inter_move(U, KL, L, 1, LookupMode::FromKeeper);     // storage key fifotbl       // out key
        STACK_CHECK(L, 2);
        lua_newtable(L);                                                                       // out key keyout
        luaG_inter_move(U, KL, L, 1, LookupMode::FromKeeper);     // storage key               // out key keyout fifotbl
        lua_pushinteger(L, fifo->first);                                                       // out key keyout fifotbl first
        STACK_CHECK(L, 5);
        lua_setfield(L, -3, "first");                                                          // out key keyout fifotbl
        lua_pushinteger(L, fifo->count);                                                       // out key keyout fifobtl count
        STACK_CHECK(L, 5);
        lua_setfield(L, -3, "count");                                                          // out key keyout fifotbl
        lua_pushinteger(L, fifo->limit);                                                       // out key keyout fifotbl limit
        STACK_CHECK(L, 5);
        lua_setfield(L, -3, "limit");                                                          // out key keyout fifotbl
        lua_setfield(L, -2, "fifo");                                                           // out key keyout
        lua_rawset(L, -3);                                                                     // out
        STACK_CHECK(L, 1);
    }
    STACK_CHECK(L, 1);
    lua_pop(KL, 1);                                               //
    STACK_CHECK(KL, 0);
    return 1;
}

// ##################################################################################################

// in: linda_ud
int keepercall_clear(lua_State* L)
{
    STACK_GROW(L, 3);
    STACK_CHECK_START_REL(L, 0);
    FIFOS_KEY.pushValue(L);                     // ud fifos
    lua_pushvalue(L, 1);                        // ud fifos ud
    lua_pushnil(L);                             // ud fifos ud nil
    lua_rawset(L, -3);                          // ud fifos
    lua_pop(L, 1);                              // ud
    STACK_CHECK(L, 0);
    return 0;
}

// ##################################################################################################

// in: linda_ud, key, ...
// out: true|false
int keepercall_send(lua_State* L)
{
    int const n{ lua_gettop(L) - 2 };
    push_table(L, 1);                             // ud key ... fifos
    // get the fifo associated to this key in this linda, create it if it doesn't exist
    lua_pushvalue(L, 2);                          // ud key ... fifos key
    lua_rawget(L, -2);                            // ud key ... fifos fifo
    if( lua_isnil(L, -1))
    {
        lua_pop(L, 1);                            // ud key ... fifos
        fifo_new(L);                              // ud key ... fifos fifo
        lua_pushvalue(L, 2);                      // ud key ... fifos fifo key
        lua_pushvalue(L, -2);                     // ud key ... fifos fifo key fifo
        lua_rawset(L, -4);                        // ud key ... fifos fifo
    }
    lua_remove(L, -2);                            // ud key ... fifo
    keeper_fifo* fifo{ keeper_fifo::getPtr(L, -1) };
    if (fifo->limit >= 0 && fifo->count + n > fifo->limit)
    {
        lua_settop(L, 0);                         //
        lua_pushboolean(L, 0);                    // false
    }
    else
    {
        fifo = prepare_fifo_access(L, -1);        // ud fifotbl
        lua_replace(L, 2);                        // ud fifotbl ...
        fifo_push(L, fifo, n);                    // ud fifotbl
        lua_settop(L, 0);                         //
        lua_pushboolean(L, 1);                    // true
    }
    return 1;
}

// ##################################################################################################

// in: linda_ud, key [, key]?
// out: (key, val) or nothing
int keepercall_receive(lua_State* L)
{
    int const top{ lua_gettop(L) };
    push_table(L, 1);                                          // ud keys fifos
    lua_replace(L, 1);                                         // fifos keys
    for (int i = 2; i <= top; ++i)
    {
        lua_pushvalue(L, i);                                   // fifos keys key[i]
        lua_rawget(L, 1);                                      // fifos keys fifo
        keeper_fifo* const fifo{ prepare_fifo_access(L, -1) }; // fifos keys fifotbl
        if (fifo != nullptr && fifo->count > 0)
        {
            fifo_pop(L, fifo, 1);                              // fifos keys val
            if (!lua_isnil(L, -1))
            {
                lua_replace(L, 1);                             // val keys
                lua_settop(L, i);                              // val keys key[i]
                if (i != 2)
                {
                    lua_replace(L, 2);                         // val key keys
                    lua_settop(L, 2);                          // val key
                }
                lua_insert(L, 1);                              // key, val
                return 2;
            }
        }
        lua_settop(L, top);                                    // data keys
    }
    // nothing to receive
    return 0;
}

// ##################################################################################################

// in: linda_ud key mincount [maxcount]
int keepercall_receive_batched(lua_State* L)
{
    int const min_count{ static_cast<int>(lua_tointeger(L, 3)) };
    if( min_count > 0)
    {
        int const max_count{ static_cast<int>(luaL_optinteger(L, 4, min_count)) };
        lua_settop(L, 2);                                         // ud key
        lua_insert(L, 1);                                         // key ud
        push_table(L, 2);                                         // key ud fifos
        lua_remove(L, 2);                                         // key fifos
        lua_pushvalue(L, 1);                                      // key fifos key
        lua_rawget(L, 2);                                         // key fifos fifo
        lua_remove(L, 2);                                         // key fifo
        keeper_fifo* const fifo{ prepare_fifo_access(L, 2) };     // key fifotbl
        if( fifo != nullptr && fifo->count >= min_count)
        {
            fifo_pop(L, fifo, std::min( max_count, fifo->count)); // key ...
        }
        else
        {
            lua_settop(L, 0);                                     //
        }
        return lua_gettop(L);
    }
    else
    {
        return 0;
    }
}

// ##################################################################################################

// in: linda_ud key n
// out: true or nil
int keepercall_limit(lua_State* L)
{
    int const limit{ static_cast<int>(lua_tointeger(L, 3)) };
    push_table(L, 1);                                   // ud key n fifos
    lua_replace(L, 1);                                  // fifos key n
    lua_pop(L, 1);                                      // fifos key
    lua_pushvalue(L, -1);                               // fifos key key
    lua_rawget(L, -3);                                  // fifos key fifo|nil
    keeper_fifo* fifo{ keeper_fifo::getPtr(L, -1) };
    if (fifo == nullptr)
    {                                                   // fifos key nil
        lua_pop(L, 1);                                  // fifos key
        fifo = fifo_new(L);                             // fifos key fifo
        lua_rawset(L, -3);                              // fifos
    }
    // remove any clutter on the stack
    lua_settop(L, 0);
    // return true if we decide that blocked threads waiting to write on that key should be awakened
    // this is the case if we detect the key was full but it is no longer the case
    if(
           ((fifo->limit >= 0) && (fifo->count >= fifo->limit)) // the key was full if limited and count exceeded the previous limit
        && ((limit < 0) || (fifo->count < limit))       // the key is not full if unlimited or count is lower than the new limit
    )
    {
        lua_pushboolean(L, 1);                          // true
    }
    // set the new limit
    fifo->limit = limit;
    // return 0 or 1 value
    return lua_gettop(L);
}

// ##################################################################################################

// in: linda_ud key [[val] ...]
//out: true or nil
int keepercall_set(lua_State* L)
{
    bool should_wake_writers{ false };
    STACK_GROW(L, 6);

    // retrieve fifos associated with the linda
    push_table(L, 1);                                      // ud key [val [, ...]] fifos
    lua_replace(L, 1);                                     // fifos key [val [, ...]]

    // make sure we have a value on the stack
    if (lua_gettop(L) == 2)                                // fifos key
    {
        lua_pushvalue(L, -1);                              // fifos key key
        lua_rawget(L, 1);                                  // fifos key fifo|nil
        // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
        keeper_fifo* const fifo{ keeper_fifo::getPtr(L, -1) };
        if (fifo != nullptr) // might be nullptr if we set a nonexistent key to nil
        {                                                  // fifos key fifo
            if (fifo->limit < 0) // fifo limit value is the default (unlimited): we can totally remove it
            {
                lua_pop(L, 1);                             // fifos key
                lua_pushnil(L);                            // fifos key nil
                lua_rawset(L, -3);                         // fifos
            }
            else
            {
                // we create room if the fifo was full but it is no longer the case
                should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit);
                lua_remove(L, -2);                         // fifos fifo
                lua_newtable(L);                           // fifos fifo {}
                lua_setiuservalue(L, -2, CONTENTS_TABLE);  // fifos fifo
                fifo->first = 1;
                fifo->count = 0;
            }
        }
    }
    else // set/replace contents stored at the specified key?
    {
        int const count{ lua_gettop(L) - 2 }; // number of items we want to store
        lua_pushvalue(L, 2);                               // fifos key [val [, ...]] key
        lua_rawget(L, 1);                                  // fifos key [val [, ...]] fifo|nil
        keeper_fifo* fifo{ keeper_fifo::getPtr(L, -1) };
        if( fifo == nullptr) // can be nullptr if we store a value at a new key
        {                                                  // fifos key [val [, ...]] nil
            // no need to wake writers in that case, because a writer can't wait on an inexistent key
            lua_pop(L, 1);                                 // fifos key [val [, ...]]
            fifo_new(L);                                   // fifos key [val [, ...]] fifo
            lua_pushvalue(L, 2);                           // fifos key [val [, ...]] fifo key
            lua_pushvalue(L, -2);                          // fifos key [val [, ...]] fifo key fifo
            lua_rawset(L, 1);                              // fifos key [val [, ...]] fifo
        }
        else // the fifo exists, we just want to update its contents
        {                                                  // fifos key [val [, ...]] fifo
            // we create room if the fifo was full but it is no longer the case
            should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit) && (count < fifo->limit);
            // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
            lua_newtable(L);                               // fifos key [val [, ...]] fifo {}
            lua_setiuservalue(L, -2, CONTENTS_TABLE);      // fifos key [val [, ...]] fifo
            fifo->first = 1;
            fifo->count = 0;
        }
        fifo = prepare_fifo_access(L, -1);                 // fifos key [val [, ...]] fifotbl
        // move the fifo below the values we want to store
        lua_insert(L, 3);                                  // fifos key fifotbl [val [, ...]]
        fifo_push(L, fifo, count);                         // fifos key fifotbl
    }
    return should_wake_writers ? (lua_pushboolean(L, 1), 1) : 0;
}

// ##################################################################################################

// in: linda_ud key [count]
// out: at most <count> values
int keepercall_get(lua_State* L)
{
    int count{ 1 };
    if (lua_gettop(L) == 3)                                // ud key count
    {
        count = static_cast<int>(lua_tointeger(L, 3));
        lua_pop(L, 1);                                     // ud key
    }
    push_table(L, 1);                                      // ud key fifos
    lua_replace(L, 1);                                     // fifos key
    lua_rawget(L, 1);                                      // fifos fifo
    keeper_fifo* const fifo{ prepare_fifo_access(L, -1) }; // fifos fifotbl
    if (fifo != nullptr && fifo->count > 0)
    {
        lua_remove(L, 1);                                  // fifotbl
        count = std::min(count, fifo->count);
        // read <count> value off the fifo
        fifo_peek(L, fifo, count);                         // fifotbl ...
        return count;
    }
    // no fifo was ever registered for this key, or it is empty
    return 0;
}

// ##################################################################################################

// in: linda_ud [, key [, ...]]
int keepercall_count(lua_State* L)
{
    push_table(L, 1);                                              // ud keys fifos
    switch (lua_gettop(L))
    {
        // no key is specified: return a table giving the count of all known keys
        case 2:                                                    // ud fifos
        lua_newtable(L);                                           // ud fifos out
        lua_replace(L, 1);                                         // out fifos
        lua_pushnil(L);                                            // out fifos nil
        while (lua_next(L, 2))                                     // out fifos key fifo
        {
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L, -1) };
            lua_pop(L, 1);                                         // out fifos key
            lua_pushvalue(L, -1);                                  // out fifos key key
            lua_pushinteger(L, fifo->count);                       // out fifos key key count
            lua_rawset(L, -5);                                     // out fifos key
        }
        lua_pop(L, 1);                                             // out
        break;

        // 1 key is specified: return its count
        case 3:                                                    // ud key fifos
        lua_replace(L, 1);                                         // fifos key
        lua_rawget(L, -2);                                         // fifos fifo|nil
        if (lua_isnil(L, -1))                                      // the key is unknown
        {                                                          // fifos nil
            lua_remove(L, -2);                                     // nil
        }
        else // the key is known
        {                                                          // fifos fifo
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L, -1) };
            lua_pushinteger(L, fifo->count);                       // fifos fifo count
            lua_replace(L, -3);                                    // count fifo
            lua_pop(L, 1);                                         // count
        }
        break;

        // a variable number of keys is specified: return a table of their counts
        default: // ud keys fifos
        lua_newtable(L);                                           // ud keys... fifos out
        lua_replace(L, 1);                                         // out keys... fifos
        // shifts all keys up in the stack. potentially slow if there are a lot of them, but then it should be bearable
        lua_insert(L, 2);                                          // out fifos keys...
        while (lua_gettop(L) > 2)
        {
            lua_pushvalue(L, -1);                                  // out fifos keys... key
            lua_rawget(L, 2);                                      // out fifos keys... fifo|nil
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L, -1) };
            lua_pop(L, 1);                                         // out fifos keys...
            if (fifo != nullptr)                                   // the key is known
            {
                lua_pushinteger(L, fifo->count);                   // out fifos keys... count
                lua_rawset(L, 1);                                  // out fifos keys...
            }
            else // the key is unknown
            {
                lua_pop(L, 1);                                     // out fifos keys...
            }
        } // all keys are exhausted                                // out fifos
        lua_pop(L, 1);                                             // out
    }
    ASSERT_L(lua_gettop(L) == 1);
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

// called as __gc for the keepers array userdata
void close_keepers(Universe* U)
{
    if (U->keepers != nullptr)
    {
        int nbKeepers = U->keepers->nb_keepers;
        // NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
        // when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
        // in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
        // which is early-outed with a U->keepers->nbKeepers null-check
        U->keepers->nb_keepers = 0;
        for (int i = 0; i < nbKeepers; ++i)
        {
            lua_State* K = U->keepers->keeper_array[i].L;
            U->keepers->keeper_array[i].L = nullptr;
            if (K != nullptr)
            {
                lua_close(K);
            }
            else
            {
                // detected partial init: destroy only the mutexes that got initialized properly
                nbKeepers = i;
            }
        }
        for (int i = 0; i < nbKeepers; ++i)
        {
            U->keepers->keeper_array[i].~Keeper();
        }
        // free the keeper bookkeeping structure
        U->internal_allocator.free(U->keepers, sizeof(Keepers) + (nbKeepers - 1) * sizeof(Keeper));
        U->keepers = nullptr;
    }
}

// ##################################################################################################

/*
 * Initialize keeper states
 *
 * If there is a problem, returns nullptr and pushes the error message on the stack
 * else returns the keepers bookkeeping structure.
 *
 * Note: Any problems would be design flaws; the created Lua state is left
 *       unclosed, because it does not really matter. In production code, this
 *       function never fails.
 * settings table is at position 1 on the stack
 */
void init_keepers(Universe* U, lua_State* L)
{
    STACK_CHECK_START_REL(L, 0);                                   // L                            K
    lua_getfield(L, 1, "nb_keepers");                              // nb_keepers
    int const nb_keepers{ static_cast<int>(lua_tointeger(L, -1)) };
    lua_pop(L, 1);                                                 //
    if (nb_keepers < 1)
    {
        std::ignore = luaL_error(L, "Bad number of keepers (%d)", nb_keepers);
    }
    STACK_CHECK(L, 0);

    lua_getfield(L, 1, "keepers_gc_threshold");                    // keepers_gc_threshold
    int const keepers_gc_threshold{ static_cast<int>(lua_tointeger(L, -1)) };
    lua_pop(L, 1);                                                 //
    STACK_CHECK(L, 0);

    // Keepers contains an array of 1 Keeper, adjust for the actual number of keeper states
    {
        size_t const bytes = sizeof(Keepers) + (nb_keepers - 1) * sizeof(Keeper);
        U->keepers = static_cast<Keepers*>(U->internal_allocator.alloc(bytes));
        if (U->keepers == nullptr)
        {
            std::ignore = luaL_error(L, "init_keepers() failed while creating keeper array; out of memory");
        }
        U->keepers->Keepers::Keepers();
        U->keepers->gc_threshold = keepers_gc_threshold;
        U->keepers->nb_keepers = nb_keepers;

        for (int i = 0; i < nb_keepers; ++i)
        {
            U->keepers->keeper_array[i].Keeper::Keeper();
        }
    }
    for (int i = 0; i < nb_keepers; ++i)                           // keepersUD
    {
        // note that we will leak K if we raise an error later
        lua_State* const K{ create_state(U, L) };
        if (K == nullptr)
        {
            std::ignore = luaL_error(L, "init_keepers() failed while creating keeper states; out of memory");
        }

        U->keepers->keeper_array[i].L = K;

        if (U->keepers->gc_threshold >= 0)
        {
            lua_gc(K, LUA_GCSTOP, 0);
        }

        STACK_CHECK_START_ABS(K, 0);

        // copy the universe pointer in the keeper itself
        universe_store(K, U);
        STACK_CHECK(K, 0);

        // make sure 'package' is initialized in keeper states, so that we have require()
        // this because this is needed when transferring deep userdata object
        luaL_requiref(K, "package", luaopen_package, 1);           //                              package
        lua_pop(K, 1);                                             //
        STACK_CHECK(K, 0);
        serialize_require(DEBUGSPEW_PARAM_COMMA(U) K);
        STACK_CHECK(K, 0);

        // copy package.path and package.cpath from the source state
        lua_getglobal(L, "package");                               // "..." keepersUD package
        if (!lua_isnil(L, -1))
        {
            // when copying with mode LookupMode::ToKeeper, error message is pushed at the top of the stack, not raised immediately
            if (luaG_inter_copy_package(U, Source{ L }, Dest{ K }, -1, LookupMode::ToKeeper))
            {
                // if something went wrong, the error message is at the top of the stack
                lua_remove(L, -2);                                 // error_msg
                raise_lua_error(L);
            }
        }
        lua_pop(L, 1);                                             //
        STACK_CHECK(L, 0);

        // attempt to call on_state_create(), if we have one and it is a C function
        // (only support a C function because we can't transfer executable Lua code in keepers)
        // will raise an error in L in case of problem
        call_on_state_create(U, K, L, LookupMode::ToKeeper);

        // to see VM name in Decoda debugger
        lua_pushfstring(K, "Keeper #%d", i + 1);                   //                              "Keeper #n"
        lua_setglobal(K, "decoda_name");                           //
        // create the fifos table in the keeper state
        FIFOS_KEY.setValue(K, [](lua_State* L) { lua_newtable(L); });
        STACK_CHECK(K, 0);
    }
    STACK_CHECK(L, 0);
}

// ##################################################################################################

// should be called only when inside a keeper_acquire/keeper_release pair (see linda_protected_call)
Keeper* which_keeper(Keepers* keepers_, uintptr_t magic_)
{
    int const nbKeepers{ keepers_->nb_keepers };
    if (nbKeepers)
    {
        unsigned int i = (unsigned int) ((magic_ >> KEEPER_MAGIC_SHIFT) % nbKeepers);
        return &keepers_->keeper_array[i];
    }
    return nullptr;
}

// ##################################################################################################

Keeper* keeper_acquire(Keepers* keepers_, uintptr_t magic_)
{
    int const nbKeepers{ keepers_->nb_keepers };
    // can be 0 if this happens during main state shutdown (lanes is being GC'ed -> no keepers)
    if (nbKeepers)
    {
        /*
        * Any hashing will do that maps pointers to 0..GNbKeepers-1 
        * consistently.
        *
        * Pointers are often aligned by 8 or so - ignore the low order bits
        * have to cast to unsigned long to avoid compilation warnings about loss of data when converting pointer-to-integer
        */
        unsigned int i = (unsigned int)((magic_ >> KEEPER_MAGIC_SHIFT) % nbKeepers);
        Keeper* K = &keepers_->keeper_array[i];
        K->m_mutex.lock();
        //++ K->count;
        return K;
    }
    return nullptr;
}

// ##################################################################################################

void keeper_release(Keeper* K)
{
    //-- K->count;
    if (K)
    {
        K->m_mutex.unlock();
    }
}

// ##################################################################################################

void keeper_toggle_nil_sentinels(lua_State* L, int val_i_, LookupMode const mode_)
{
    int const n{ lua_gettop(L) };
    for (int i = val_i_; i <= n; ++i)
    {
        if (mode_ == LookupMode::ToKeeper)
        {
            if (lua_isnil(L, i))
            {
                NIL_SENTINEL.pushKey(L);
                lua_replace(L, i);
            }
        }
        else
        {
            if (NIL_SENTINEL.equals(L, i))
            {
                lua_pushnil(L);
                lua_replace(L, i);
            }
        }
    }
}

// ##################################################################################################

/*
* Call a function ('func_name') in the keeper state, and pass on the returned
* values to 'L'.
*
* 'linda':          deep Linda pointer (used only as a unique table key, first parameter)
* 'starting_index': first of the rest of parameters (none if 0)
*
* Returns: number of return values (pushed to 'L') or -1 in case of error
*/
int keeper_call(Universe* U, lua_State* K, keeper_api_t func_, lua_State* L, void* linda, int starting_index)
{
    int const args{ starting_index ? (lua_gettop(L) - starting_index + 1) : 0 };
    int const Ktos{ lua_gettop(K) };
    int retvals = -1;

    STACK_GROW(K, 2);

    PUSH_KEEPER_FUNC(K, func_);

    lua_pushlightuserdata(K, linda);

    if ((args == 0) || luaG_inter_copy(U, Source{ L }, Dest{ K }, args, LookupMode::ToKeeper) == 0) // L->K
    {
        lua_call(K, 1 + args, LUA_MULTRET);
        retvals = lua_gettop(K) - Ktos;
        // note that this can raise a luaL_error while the keeper state (and its mutex) is acquired
        // this may interrupt a lane, causing the destruction of the underlying OS thread
        // after this, another lane making use of this keeper can get an error code from the mutex-locking function
        // when attempting to grab the mutex again (WINVER <= 0x400 does this, but locks just fine, I don't know about pthread)
        if ((retvals > 0) && luaG_inter_move(U, Source{ K }, Dest{ L }, retvals, LookupMode::FromKeeper) != 0) // K->L
        {
            retvals = -1;
        }
    }
    // whatever happens, restore the stack to where it was at the origin
    lua_settop(K, Ktos);

    // don't do this for this particular function, as it is only called during Linda destruction, and we don't want to raise an error, ever
    if (func_ != KEEPER_API(clear)) [[unlikely]]
    {
        // since keeper state GC is stopped, let's run a step once in a while if required
        int const gc_threshold{ U->keepers->gc_threshold };
        if (gc_threshold == 0) [[unlikely]]
        {
            lua_gc(K, LUA_GCSTEP, 0);
        }
        else if (gc_threshold > 0) [[likely]]
        {
            int const gc_usage{ lua_gc(K, LUA_GCCOUNT, 0) };
            if (gc_usage >= gc_threshold)
            {
                lua_gc(K, LUA_GCCOLLECT, 0);
                int const gc_usage_after{ lua_gc(K, LUA_GCCOUNT, 0) };
                if (gc_usage_after > gc_threshold) [[unlikely]]
                {
                    luaL_error(L, "Keeper GC threshold is too low, need at least %d", gc_usage_after);
                }
            }
        }
    }

    return retvals;
}
