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
#include "linda.h"
#include "state.h"
#include "tools.h"
#include "uniquekey.h"
#include "universe.h"

#include <algorithm>
#include <cassert>

// #################################################################################################
// Keeper implementation
// #################################################################################################

class keeper_fifo
{
    public:
    int first{ 1 };
    int count{ 0 };
    int limit{ -1 };

    // a fifo full userdata has one uservalue, the table that holds the actual fifo contents
    [[nodiscard]] static void* operator new([[maybe_unused]] size_t size_, KeeperState L_) noexcept { return lua_newuserdatauv<keeper_fifo>(L_, 1); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* p_, KeeperState L_) { LUA_ASSERT(L_, !"should never be called"); }

    [[nodiscard]] static keeper_fifo* getPtr(lua_State* L_, int idx_)
    {
        return lua_tofulluserdata<keeper_fifo>(L_, idx_);
    }
};

static constexpr int kContentsTableIndex{ 1 };

// #################################################################################################

// replaces the fifo ud by its uservalue on the stack
[[nodiscard]] static keeper_fifo* prepare_fifo_access(lua_State* L_, int idx_)
{
    keeper_fifo* const fifo{ keeper_fifo::getPtr(L_, idx_) };
    if (fifo != nullptr) {
        idx_ = lua_absindex(L_, idx_);
        STACK_GROW(L_, 1);
        // we can replace the fifo userdata in the stack without fear of it being GCed, there are other references around
        lua_getiuservalue(L_, idx_, kContentsTableIndex);
        lua_replace(L_, idx_);
    }
    return fifo;
}

// #################################################################################################

// in: nothing
// out: { first = 1, count = 0, limit = -1}
[[nodiscard]] static keeper_fifo* fifo_new(KeeperState L_)
{
    STACK_GROW(L_, 2);
    STACK_CHECK_START_REL(L_, 0);
    keeper_fifo* const fifo{ new (L_) keeper_fifo{} };
    STACK_CHECK(L_, 1);
    lua_newtable(L_);
    lua_setiuservalue(L_, -2, kContentsTableIndex);
    STACK_CHECK(L_, 1);
    return fifo;
}

// #################################################################################################

// in: expect fifo ... on top of the stack
// out: nothing, removes all pushed values from the stack
static void fifo_push(lua_State* L_, keeper_fifo* fifo_, int count_)
{
    int const idx{ lua_gettop(L_) - count_ };
    int const start{ fifo_->first + fifo_->count - 1 };
    // pop all additional arguments, storing them in the fifo
    for (int i = count_; i >= 1; --i) {
        // store in the fifo the value at the top of the stack at the specified index, popping it from the stack
        lua_rawseti(L_, idx, start + i);
    }
    fifo_->count += count_;
}

// #################################################################################################

// in: fifo
// out: ...|nothing
// expects exactly 1 value on the stack!
// currently only called with a count of 1, but this may change in the future
// function assumes that there is enough data in the fifo to satisfy the request
static void fifo_peek(lua_State* L_, keeper_fifo* fifo_, int count_)
{
    STACK_GROW(L_, count_);
    for (int i = 0; i < count_; ++i) {
        lua_rawgeti(L_, 1, (fifo_->first + i));
    }
}

// #################################################################################################

// in: fifo
// out: remove the fifo from the stack, push as many items as required on the stack (function assumes they exist in sufficient number)
static void fifo_pop(lua_State* L_, keeper_fifo* fifo_, int count_)
{
    LUA_ASSERT(L_, lua_istable(L_, -1));
    int const fifo_idx{ lua_gettop(L_) };                                                          // L_: ... fifotbl
    // each iteration pushes a value on the stack!
    STACK_GROW(L_, count_ + 2);
    // skip first item, we will push it last
    for (int i = 1; i < count_; ++i) {
        int const at{ fifo_->first + i };
        // push item on the stack
        lua_rawgeti(L_, fifo_idx, at);                                                             // L_: ... fifotbl val
        // remove item from the fifo
        lua_pushnil(L_);                                                                           // L_: ... fifotbl val nil
        lua_rawseti(L_, fifo_idx, at);                                                             // L_: ... fifotbl val
    }
    // now process first item
    {
        int const at{ fifo_->first };
        lua_rawgeti(L_, fifo_idx, at);                                                             // L_: ... fifotbl vals val
        lua_pushnil(L_);                                                                           // L_: ... fifotbl vals val nil
        lua_rawseti(L_, fifo_idx, at);                                                             // L_: ... fifotbl vals val
        lua_replace(L_, fifo_idx);                                                                 // L_: ... vals
    }

    // avoid ever-growing indexes by resetting each time we detect the fifo is empty
    {
        int const new_count{ fifo_->count - count_ };
        fifo_->first = (new_count == 0) ? 1 : (fifo_->first + count_);
        fifo_->count = new_count;
    }
}

// #################################################################################################

// in: linda_ud expected at stack slot idx
// out: fifos[ud]
// xxh64 of string "kFifosRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kFifosRegKey{ 0x37F11CE5A6D191AAull };
static void push_table(lua_State* L_, int idx_)
{
    STACK_GROW(L_, 5);
    STACK_CHECK_START_REL(L_, 0);
    idx_ = lua_absindex(L_, idx_);
    kFifosRegKey.pushValue(L_);                                                                    // L_: ud fifos
    lua_pushvalue(L_, idx_);                                                                       // L_: ud fifos ud
    lua_rawget(L_, -2);                                                                            // L_: ud fifos fifos[ud]
    STACK_CHECK(L_, 2);
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);                                                                            // L_: ud fifos
        // add a new fifos table for this linda
        lua_newtable(L_);                                                                          // L_: ud fifos fifos[ud]
        lua_pushvalue(L_, idx_);                                                                   // L_: ud fifos fifos[ud] ud
        lua_pushvalue(L_, -2);                                                                     // L_: ud fifos fifos[ud] ud fifos[ud]
        lua_rawset(L_, -4);                                                                        // L_: ud fifos fifos[ud]
    }
    lua_remove(L_, -2);                                                                            // L_: ud fifos[ud]
    STACK_CHECK(L_, 1);
}

// #################################################################################################

// only used by linda:dump() and linda:__towatch() for debugging purposes
int keeper_push_linda_storage(Linda& linda_, DestState L_)
{
    Keeper* const K{ linda_.whichKeeper() };
    SourceState const KL{ K ? K->L : nullptr };
    if (KL == nullptr)
        return 0;
    STACK_GROW(KL, 4);
    STACK_CHECK_START_REL(KL, 0);
    kFifosRegKey.pushValue(KL);                                                                    // KL: fifos                                          L_:
    lua_pushlightuserdata(KL, &linda_);                                                            // KL: fifos ud                                       L_:
    lua_rawget(KL, -2);                                                                            // KL: fifos storage                                  L_:
    lua_remove(KL, -2);                                                                            // KL: storage                                        L_:
    if (!lua_istable(KL, -1)) {
        lua_pop(KL, 1);                                                                            // KL:                                                L_:
        STACK_CHECK(KL, 0);
        return 0;
    }
    // move data from keeper to destination state
    STACK_GROW(L_, 5);
    STACK_CHECK_START_REL(L_, 0);
    lua_newtable(L_);                                                                              // KL: storage                                        L_: out
    InterCopyContext c{ linda_.U, L_, KL, {}, {}, {}, LookupMode::FromKeeper, {} };
    lua_pushnil(KL);                                                                               // KL: storage nil                                    L_: out
    while (lua_next(KL, -2)) {                                                                     // KL: storage key fifo                               L_: out
        keeper_fifo* fifo = prepare_fifo_access(KL, -1);                                           // KL: storage key fifotbl                            L_: out
        lua_pushvalue(KL, -2);                                                                     // KL: storage key fifotbl key                        L_: out
        std::ignore = c.inter_move(1);                                                             // KL: storage key fifotbl                            L_: out key
        STACK_CHECK(L_, 2);
        lua_newtable(L_);                                                                          // KL: storage key                                    L_: out key keyout
        std::ignore = c.inter_move(1);                                                             // KL: storage key                                    L_: out key keyout fifotbl
        lua_pushinteger(L_, fifo->first);                                                          // KL: storage key                                    L_: out key keyout fifotbl first
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "first");                                                             // KL: storage key                                    L_: out key keyout fifotbl
        lua_pushinteger(L_, fifo->count);                                                          // KL: storage key                                    L_: out key keyout fifobtl count
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "count");                                                             // KL: storage key                                    L_: out key keyout fifotbl
        lua_pushinteger(L_, fifo->limit);                                                          // KL: storage key                                    L_: out key keyout fifotbl limit
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "limit");                                                             // KL: storage key                                    L_: out key keyout fifotbl
        lua_setfield(L_, -2, "fifo");                                                              // KL: storage key                                    L_: out key keyout
        lua_rawset(L_, -3);                                                                        // KL: storage key                                    L_: out
        STACK_CHECK(L_, 1);
    }                                                                                              // KL_: storage                                       L_: out
    STACK_CHECK(L_, 1);
    lua_pop(KL, 1);                                                                                // KL:                                                L_: out
    STACK_CHECK(KL, 0);
    return 1;
}

// #################################################################################################

// in: linda_ud
int keepercall_clear(lua_State* L_)
{
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    kFifosRegKey.pushValue(L_);                                                                    // L_: ud fifos
    lua_pushvalue(L_, 1);                                                                          // L_: ud fifos ud
    lua_pushnil(L_);                                                                               // L_: ud fifos ud nil
    lua_rawset(L_, -3);                                                                            // L_: ud fifos
    lua_pop(L_, 1);                                                                                // L_: ud
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

// in: linda_ud, key, ...
// out: true|false
int keepercall_send(lua_State* L_)
{
    int const n{ lua_gettop(L_) - 2 };
    push_table(L_, 1);                                                                             // L_: ud key ... fifos
    // get the fifo associated to this key in this linda, create it if it doesn't exist
    lua_pushvalue(L_, 2);                                                                          // L_: ud key ... fifos key
    lua_rawget(L_, -2);                                                                            // L_: ud key ... fifos fifo
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);                                                                            // L_: ud key ... fifos
        std::ignore = fifo_new(KeeperState{ L_ });                                                 // L_: ud key ... fifos fifo
        lua_pushvalue(L_, 2);                                                                      // L_: ud key ... fifos fifo key
        lua_pushvalue(L_, -2);                                                                     // L_: ud key ... fifos fifo key fifo
        lua_rawset(L_, -4);                                                                        // L_: ud key ... fifos fifo
    }
    lua_remove(L_, -2);                                                                            // L_: ud key ... fifo
    keeper_fifo* fifo{ keeper_fifo::getPtr(L_, -1) };
    if (fifo->limit >= 0 && fifo->count + n > fifo->limit) {
        lua_settop(L_, 0);                                                                         // L_:
        lua_pushboolean(L_, 0);                                                                    // L_:false
    } else {
        fifo = prepare_fifo_access(L_, -1);                                                        // L_: ud fifotbl
        lua_replace(L_, 2);                                                                        // L_: ud fifotbl ...
        fifo_push(L_, fifo, n);                                                                    // L_: ud fifotbl
        lua_settop(L_, 0);                                                                         // L_:
        lua_pushboolean(L_, 1);                                                                    // L_: true
    }
    return 1;
}

// #################################################################################################

// in: linda_ud, key [, key]?
// out: (key, val) or nothing
int keepercall_receive(lua_State* L_)
{
    int const top{ lua_gettop(L_) };
    push_table(L_, 1);  // L_: ud keys fifos
    lua_replace(L_, 1); // L_: fifos keys
    for (int i = 2; i <= top; ++i) {
        lua_pushvalue(L_, i);                                                                      // L_: fifos keys key[i]
        lua_rawget(L_, 1);                                                                         // L_: fifos keys fifo
        keeper_fifo* const fifo{ prepare_fifo_access(L_, -1) };                                    // L_: fifos keys fifotbl
        if (fifo != nullptr && fifo->count > 0) {
            fifo_pop(L_, fifo, 1);                                                                 // L_: fifos keys val
            if (!lua_isnil(L_, -1)) {
                lua_replace(L_, 1);                                                                // L_: val keys
                lua_settop(L_, i);                                                                 // L_: val keys key[i]
                if (i != 2) {
                    lua_replace(L_, 2);                                                            // L_: val key keys
                    lua_settop(L_, 2);                                                             // L_: val key
                }
                lua_insert(L_, 1);                                                                 // L_: key, val
                return 2;
            }
        }
        lua_settop(L_, top);                                                                       // L_: data keys
    }
    // nothing to receive
    return 0;
}

// #################################################################################################

// in: linda_ud key mincount [maxcount]
int keepercall_receive_batched(lua_State* L_)
{
    int const min_count{ static_cast<int>(lua_tointeger(L_, 3)) };
    if (min_count > 0) {
        int const max_count{ static_cast<int>(luaL_optinteger(L_, 4, min_count)) };
        lua_settop(L_, 2);                                                                         // L_: ud key
        lua_insert(L_, 1);                                                                         // L_: key ud
        push_table(L_, 2);                                                                         // L_: key ud fifos
        lua_remove(L_, 2);                                                                         // L_: key fifos
        lua_pushvalue(L_, 1);                                                                      // L_: key fifos key
        lua_rawget(L_, 2);                                                                         // L_: key fifos fifo
        lua_remove(L_, 2);                                                                         // L_: key fifo
        keeper_fifo* const fifo{ prepare_fifo_access(L_, 2) };                                     // L_: key fifotbl
        if (fifo != nullptr && fifo->count >= min_count) {
            fifo_pop(L_, fifo, std::min(max_count, fifo->count));                                  // L_: key ...
        } else {
            lua_settop(L_, 0);                                                                     // L_:
        }
        return lua_gettop(L_);
    } else {
        return 0;
    }
}

// #################################################################################################

// in: linda_ud key n
// out: true or nil
int keepercall_limit(lua_State* L_)
{
    int const limit{ static_cast<int>(lua_tointeger(L_, 3)) };
    push_table(L_, 1);                                                                             // L_: ud key n fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key n
    lua_pop(L_, 1);                                                                                // L_: fifos key
    lua_pushvalue(L_, -1);                                                                         // L_: fifos key key
    lua_rawget(L_, -3);                                                                            // L_: fifos key fifo|nil
    keeper_fifo* fifo{ keeper_fifo::getPtr(L_, -1) };
    if (fifo == nullptr) {                                                                         // L_: fifos key nil
        lua_pop(L_, 1);                                                                            // L_: fifos key
        fifo = fifo_new(KeeperState{ L_ });                                                        // L_: fifos key fifo
        lua_rawset(L_, -3);                                                                        // L_: fifos
    }
    // remove any clutter on the stack
    lua_settop(L_, 0);                                                                             // L_:
    // return true if we decide that blocked threads waiting to write on that key should be awakened
    // this is the case if we detect the key was full but it is no longer the case
    if (
        ((fifo->limit >= 0) && (fifo->count >= fifo->limit)) // the key was full if limited and count exceeded the previous limit
        && ((limit < 0) || (fifo->count < limit))            // the key is not full if unlimited or count is lower than the new limit
    ) {
        lua_pushboolean(L_, 1);                                                                    // L_: true
    }
    // set the new limit
    fifo->limit = limit;
    // return 0 or 1 value
    return lua_gettop(L_);
}

// #################################################################################################

// in: linda_ud key [[val] ...]
// out: true if the linda was full but it's no longer the case, else nothing
int keepercall_set(lua_State* L_)
{
    bool should_wake_writers{ false };
    STACK_GROW(L_, 6);

    // retrieve fifos associated with the linda
    push_table(L_, 1);                                                                             // L_: ud key [val [, ...]] fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key [val [, ...]]

    // make sure we have a value on the stack
    if (lua_gettop(L_) == 2) {                                                                     // L_: fifos key
        lua_pushvalue(L_, -1);                                                                     // L_: fifos key key
        lua_rawget(L_, 1);                                                                         // L_: fifos key fifo|nil
        // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
        keeper_fifo* const fifo{ keeper_fifo::getPtr(L_, -1) };
        if (fifo != nullptr) { // might be nullptr if we set a nonexistent key to nil              // L_: fifos key fifo
            if (fifo->limit < 0) {                                                                 // fifo limit value is the default (unlimited): we can totally remove it
                lua_pop(L_, 1);                                                                    // L_: fifos key
                lua_pushnil(L_);                                                                   // L_: fifos key nil
                lua_rawset(L_, -3);                                                                // L_: fifos
            } else {
                // we create room if the fifo was full but it is no longer the case
                should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit);
                lua_remove(L_, -2);                                                                // L_: fifos fifo
                lua_newtable(L_);                                                                  // L_: fifos fifo {}
                lua_setiuservalue(L_, -2, kContentsTableIndex);                                    // L_: fifos fifo
                fifo->first = 1;
                fifo->count = 0;
            }
        }
    } else { // set/replace contents stored at the specified key?
        int const count{ lua_gettop(L_) - 2 };                                                     // number of items we want to store
        lua_pushvalue(L_, 2);                                                                      // L_: fifos key [val [, ...]] key
        lua_rawget(L_, 1);                                                                         // L_: fifos key [val [, ...]] fifo|nil
        keeper_fifo* fifo{ keeper_fifo::getPtr(L_, -1) };
        if (fifo == nullptr) { // can be nullptr if we store a value at a new key                  // fifos key [val [, ...]] nil
            // no need to wake writers in that case, because a writer can't wait on an inexistent key
            lua_pop(L_, 1);                                                                        // L_: fifos key [val [, ...]]
            std::ignore = fifo_new(KeeperState{ L_ });                                             // L_: fifos key [val [, ...]] fifo
            lua_pushvalue(L_, 2);                                                                  // L_: fifos key [val [, ...]] fifo key
            lua_pushvalue(L_, -2);                                                                 // L_: fifos key [val [, ...]] fifo key fifo
            lua_rawset(L_, 1);                                                                     // L_: fifos key [val [, ...]] fifo
        } else {                                                                                   // L_: fifos key [val [, ...]] fifo
            // the fifo exists, we just want to update its contents
            // we create room if the fifo was full but it is no longer the case
            should_wake_writers = (fifo->limit > 0) && (fifo->count >= fifo->limit) && (count < fifo->limit);
            // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
            lua_newtable(L_);                                                                      // L_: fifos key [val [, ...]] fifo {}
            lua_setiuservalue(L_, -2, kContentsTableIndex);                                        // L_: fifos key [val [, ...]] fifo
            fifo->first = 1;
            fifo->count = 0;
        }
        fifo = prepare_fifo_access(L_, -1);                                                        // L_: fifos key [val [, ...]] fifotbl
        // move the fifo below the values we want to store
        lua_insert(L_, 3);                                                                         // L_: fifos key fifotbl [val [, ...]]
        fifo_push(L_, fifo, count);                                                                // L_: fifos key fifotbl
    }
    return should_wake_writers ? (lua_pushboolean(L_, 1), 1) : 0;
}

// #################################################################################################

// in: linda_ud key [count]
// out: at most <count> values
int keepercall_get(lua_State* L_)
{
    int count{ 1 };
    if (lua_gettop(L_) == 3) {                                                                     // L_: ud key count
        count = static_cast<int>(lua_tointeger(L_, 3));
        lua_pop(L_, 1);                                                                            // L_: ud key
    }
    push_table(L_, 1);                                                                             // L_: ud key fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key
    lua_rawget(L_, 1);                                                                             // L_: fifos fifo
    keeper_fifo* const fifo{ prepare_fifo_access(L_, -1) };                                        // L_: fifos fifotbl
    if (fifo != nullptr && fifo->count > 0) {
        lua_remove(L_, 1);                                                                         // L_: fifotbl
        count = std::min(count, fifo->count);
        // read <count> value off the fifo
        fifo_peek(L_, fifo, count);                                                                // L_: fifotbl ...
        return count;
    }
    // no fifo was ever registered for this key, or it is empty
    return 0;
}

// #################################################################################################

// in: linda_ud [, key [, ...]]
int keepercall_count(lua_State* L_)
{
    push_table(L_, 1);                                                                             // L_: ud keys fifos
    switch (lua_gettop(L_)) {
    // no key is specified: return a table giving the count of all known keys
    case 2:                                                                                        // L_: ud fifos
        lua_newtable(L_);                                                                          // L_: ud fifos out
        lua_replace(L_, 1);                                                                        // L_: out fifos
        lua_pushnil(L_);                                                                           // L_: out fifos nil
        while (lua_next(L_, 2)) {                                                                  // L_: out fifos key fifo
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pop(L_, 1);                                                                        // L_: out fifos key
            lua_pushvalue(L_, -1);                                                                 // L_: out fifos key key
            lua_pushinteger(L_, fifo->count);                                                      // L_: out fifos key key count
            lua_rawset(L_, -5);                                                                    // L_: out fifos key
        }
        lua_pop(L_, 1);                                                                            // L_: out
        break;

    // 1 key is specified: return its count
    case 3:                                                                                        // L_: ud key fifos
        lua_replace(L_, 1);                                                                        // L_: fifos key
        lua_rawget(L_, -2);                                                                        // L_: fifos fifo|nil
        if (lua_isnil(L_, -1)) { // L_: the key is unknown                                         // L_: fifos nil
            lua_remove(L_, -2);                                                                    // L_: nil
        } else { // the key is known                                                               // L_: fifos fifo
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pushinteger(L_, fifo->count);                                                      // L_: fifos fifo count
            lua_replace(L_, -3);                                                                   // L_: count fifo
            lua_pop(L_, 1);                                                                        // L_: count
        }
        break;

    // a variable number of keys is specified: return a table of their counts
    default:                                                                                       // ud keys fifos
        lua_newtable(L_);                                                                          // L_: ud keys... fifos out
        lua_replace(L_, 1);                                                                        // L_: out keys... fifos
        // shifts all keys up in the stack. potentially slow if there are a lot of them, but then it should be bearable
        lua_insert(L_, 2);                                                                         // L_: out fifos keys...
        while (lua_gettop(L_) > 2) {
            lua_pushvalue(L_, -1);                                                                 // L_: out fifos keys... key
            lua_rawget(L_, 2);                                                                     // L_: out fifos keys... fifo|nil
            keeper_fifo* const fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pop(L_, 1);                                                                        // L_: out fifos keys...
            if (fifo != nullptr) {                                                                 // L_: the key is known
                lua_pushinteger(L_, fifo->count);                                                  // L_: out fifos keys... count
                lua_rawset(L_, 1);                                                                 // L_: out fifos keys...
            } else {                                                                               // the key is unknown
                lua_pop(L_, 1);                                                                    // L_: out fifos keys...
            }
        } // all keys are exhausted                                                                // L_: out fifos
        lua_pop(L_, 1);                                                                            // L_: out
    }
    LUA_ASSERT(L_, lua_gettop(L_) == 1);
    return 1;
}

// #################################################################################################
// Keeper API, accessed from linda methods
// #################################################################################################

/*
 * Pool of keeper states
 *
 * Access to keeper states is locked (only one OS thread at a time) so the
 * bigger the pool, the less chances of unnecessary waits. Lindas map to the
 * keepers randomly, by a hash.
 */

// called as __gc for the keepers array userdata
void close_keepers(Universe* U_)
{
    if (U_->keepers != nullptr) {
        int nbKeepers{ U_->keepers->nb_keepers };
        // NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
        // when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
        // in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
        // which is early-outed with a U->keepers->nbKeepers null-check
        U_->keepers->nb_keepers = 0;
        for (int i = 0; i < nbKeepers; ++i) {
            lua_State* const K{ U_->keepers->keeper_array[i].L };
            U_->keepers->keeper_array[i].L = KeeperState{ nullptr };
            if (K != nullptr) {
                lua_close(K);
            } else {
                // detected partial init: destroy only the mutexes that got initialized properly
                nbKeepers = i;
            }
        }
        for (int i = 0; i < nbKeepers; ++i) {
            U_->keepers->keeper_array[i].~Keeper();
        }
        // free the keeper bookkeeping structure
        U_->internal_allocator.free(U_->keepers, sizeof(Keepers) + (nbKeepers - 1) * sizeof(Keeper));
        U_->keepers = nullptr;
    }
}

// #################################################################################################

/*
 * Initialize keeper states
 *
 * If there is a problem, returns nullptr and pushes the error message on the stack
 * else returns the keepers bookkeeping structure.
 *
 * Note: Any problems would be design flaws; the created Lua state is left
 *       unclosed, because it does not really matter. In production code, this
 *       function never fails.
 * settings table is expected at position 1 on the stack
 */
void init_keepers(Universe* U_, lua_State* L_)
{
    LUA_ASSERT(L_, lua_gettop(L_) == 1 && lua_istable(L_, 1));
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: settings
    lua_getfield(L_, 1, "nb_keepers");                                                             // L_: settings nb_keepers
    int const nb_keepers{ static_cast<int>(lua_tointeger(L_, -1)) };
    lua_pop(L_, 1);                                                                                // L_: settings
    if (nb_keepers < 1) {
        raise_luaL_error(L_, "Bad number of keepers (%d)", nb_keepers);
    }
    STACK_CHECK(L_, 0);

    lua_getfield(L_, 1, "keepers_gc_threshold");                                                   // L_: settings keepers_gc_threshold
    int const keepers_gc_threshold{ static_cast<int>(lua_tointeger(L_, -1)) };
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 0);

    // Keepers contains an array of 1 Keeper, adjust for the actual number of keeper states
    {
        size_t const bytes = sizeof(Keepers) + (nb_keepers - 1) * sizeof(Keeper);
        U_->keepers = static_cast<Keepers*>(U_->internal_allocator.alloc(bytes));
        if (U_->keepers == nullptr) {
            raise_luaL_error(L_, "init_keepers() failed while creating keeper array; out of memory");
        }
        U_->keepers->Keepers::Keepers();
        U_->keepers->gc_threshold = keepers_gc_threshold;
        U_->keepers->nb_keepers = nb_keepers;

        for (int i = 0; i < nb_keepers; ++i) {
            U_->keepers->keeper_array[i].Keeper::Keeper();
        }
    }
    for (int i = 0; i < nb_keepers; ++i) {
        // note that we will leak K if we raise an error later
        KeeperState const K{ create_state(U_, L_) };                                               // L_: settings                                    K:
        if (K == nullptr) {
            raise_luaL_error(L_, "init_keepers() failed while creating keeper states; out of memory");
        }

        U_->keepers->keeper_array[i].L = K;

        if (U_->keepers->gc_threshold >= 0) {
            lua_gc(K, LUA_GCSTOP, 0);
        }

        STACK_CHECK_START_ABS(K, 0);

        // copy the universe pointer in the keeper itself
        universe_store(K, U_);
        STACK_CHECK(K, 0);

        // make sure 'package' is initialized in keeper states, so that we have require()
        // this because this is needed when transferring deep userdata object
        luaL_requiref(K, "package", luaopen_package, 1);                                           // L_: settings                                    K: package
        lua_pop(K, 1);                                                                             // L_: settings                                    K:
        STACK_CHECK(K, 0);
        serialize_require(DEBUGSPEW_PARAM_COMMA(U_) K);
        STACK_CHECK(K, 0);

        // copy package.path and package.cpath from the source state (TODO: use _R._LOADED.package instead of _G.package)
        lua_getglobal(L_, "package");                                                              // L_: settings package                            K:
        if (!lua_isnil(L_, -1)) {
            // when copying with mode LookupMode::ToKeeper, error message is pushed at the top of the stack, not raised immediately
            InterCopyContext c{ U_, DestState{ K }, SourceState{ L_ }, {}, SourceIndex{ lua_absindex(L_, -1) }, {}, LookupMode::ToKeeper, {} };
            if (c.inter_copy_package() != InterCopyResult::Success) {                              // L_: settings ... error_msg                      K:
                // if something went wrong, the error message is at the top of the stack
                lua_remove(L_, -2);                                                                // L_: settings error_msg
                raise_lua_error(L_);
            }
        }
        lua_pop(L_, 1);                                                                            // L_: settings                                    K:
        STACK_CHECK(L_, 0);
        STACK_CHECK(K, 0);

        // attempt to call on_state_create(), if we have one and it is a C function
        // (only support a C function because we can't transfer executable Lua code in keepers)
        // will raise an error in L_ in case of problem
        call_on_state_create(U_, K, L_, LookupMode::ToKeeper);

        // to see VM name in Decoda debugger
        lua_pushfstring(K, "Keeper #%d", i + 1);                                                   // L_: settings                                    K: "Keeper #n"
        lua_setglobal(K, "decoda_name");                                                           // L_: settings                                    K:
        // create the fifos table in the keeper state
        kFifosRegKey.setValue(K, [](lua_State* L_) { lua_newtable(L_); });                         // L_: settings                                    K:
        STACK_CHECK(K, 0);
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

Keeper* Linda::acquireKeeper() const
{
    int const nbKeepers{ U->keepers->nb_keepers };
    // can be 0 if this happens during main state shutdown (lanes is being GC'ed -> no keepers)
    if (nbKeepers) {
        Keeper* const K{ &U->keepers->keeper_array[m_keeper_index] };
        K->m_mutex.lock();
        return K;
    }
    return nullptr;
}

// #################################################################################################

void Linda::releaseKeeper(Keeper* K_) const
{
    if (K_) { // can be nullptr if we tried to acquire during shutdown
        assert(K_ == &U->keepers->keeper_array[m_keeper_index]);
        K_->m_mutex.unlock();
    }
}

// #################################################################################################

void keeper_toggle_nil_sentinels(lua_State* L_, int start_, LookupMode const mode_)
{
    int const n{ lua_gettop(L_) };
    for (int i = start_; i <= n; ++i) {
        if (mode_ == LookupMode::ToKeeper) {
            if (lua_isnil(L_, i)) {
                kNilSentinel.pushKey(L_);
                lua_replace(L_, i);
            }
        } else {
            if (kNilSentinel.equals(L_, i)) {
                lua_pushnil(L_);
                lua_replace(L_, i);
            }
        }
    }
}

// #################################################################################################

/*
 * Call a function ('func_name') in the keeper state, and pass on the returned
 * values to 'L'.
 *
 * 'linda':          deep Linda pointer (used only as a unique table key, first parameter)
 * 'starting_index': first of the rest of parameters (none if 0)
 *
 * Returns: number of return values (pushed to 'L'), unset in case of error
 */
KeeperCallResult keeper_call(Universe* U_, KeeperState K_, keeper_api_t func_, lua_State* L_, void* linda_, int starting_index_)
{
    KeeperCallResult result;
    int const args{ starting_index_ ? (lua_gettop(L_) - starting_index_ + 1) : 0 };                // L: ... args...                                  K_:
    int const top_K{ lua_gettop(K_) };
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, top_K == 0);

    STACK_GROW(K_, 2);
    PUSH_KEEPER_FUNC(K_, func_);                                                                   // L: ... args...                                  K_: func_
    lua_pushlightuserdata(K_, linda_);                                                             // L: ... args...                                  K_: func_ linda
    if (
        (args == 0) ||
        (InterCopyContext{ U_, DestState{ K_ }, SourceState{ L_ }, {}, {}, {}, LookupMode::ToKeeper, {} }.inter_copy(args) == InterCopyResult::Success)
    ) {                                                                                            // L: ... args...                                  K_: func_ linda args...
        lua_call(K_, 1 + args, LUA_MULTRET);                                                       // L: ... args...                                  K_: result...
        int const retvals{ lua_gettop(K_) - top_K };
        // note that this can raise a lua error while the keeper state (and its mutex) is acquired
        // this may interrupt a lane, causing the destruction of the underlying OS thread
        // after this, another lane making use of this keeper can get an error code from the mutex-locking function
        // when attempting to grab the mutex again (WINVER <= 0x400 does this, but locks just fine, I don't know about pthread)
        if (
            (retvals == 0) ||
            (InterCopyContext{ U_, DestState{ L_ }, SourceState{ K_ }, {}, {}, {}, LookupMode::FromKeeper, {} }.inter_move(retvals) == InterCopyResult::Success)
        ) {                                                                                        // L: ... args... result...                        K_: result...
            result.emplace(retvals);
        }
    }
    // whatever happens, restore the stack to where it was at the origin
    lua_settop(K_, top_K);                                                                         // L: ... args... result...                        K_:

    // don't do this for this particular function, as it is only called during Linda destruction, and we don't want to raise an error, ever
    if (func_ != KEEPER_API(clear)) [[unlikely]] {
        // since keeper state GC is stopped, let's run a step once in a while if required
        int const gc_threshold{ U_->keepers->gc_threshold };
        if (gc_threshold == 0) [[unlikely]] {
            lua_gc(K_, LUA_GCSTEP, 0);
        } else if (gc_threshold > 0) [[likely]] {
            int const gc_usage{ lua_gc(K_, LUA_GCCOUNT, 0) };
            if (gc_usage >= gc_threshold) {
                lua_gc(K_, LUA_GCCOLLECT, 0);
                int const gc_usage_after{ lua_gc(K_, LUA_GCCOUNT, 0) };
                if (gc_usage_after > gc_threshold) [[unlikely]] {
                    raise_luaL_error(L_, "Keeper GC threshold is too low, need at least %d", gc_usage_after);
                }
            }
        }
    }

    return result;
}
