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

#include "intercopycontext.h"
#include "linda.h"
#include "state.h"

#include <algorithm>
#include <cassert>
#include <ranges>

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
    static void operator delete([[maybe_unused]] void* p_, [[maybe_unused]] KeeperState L_) { LUA_ASSERT(L_, !"should never be called"); }

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
    keeper_fifo* const _fifo{ keeper_fifo::getPtr(L_, idx_) };
    if (_fifo != nullptr) {
        idx_ = lua_absindex(L_, idx_);
        STACK_GROW(L_, 1);
        // we can replace the fifo userdata in the stack without fear of it being GCed, there are other references around
        lua_getiuservalue(L_, idx_, kContentsTableIndex);
        lua_replace(L_, idx_);
    }
    return _fifo;
}

// #################################################################################################

// in: nothing
// out: { first = 1, count = 0, limit = -1}
[[nodiscard]] static keeper_fifo* fifo_new(KeeperState L_)
{
    STACK_GROW(L_, 2);
    STACK_CHECK_START_REL(L_, 0);
    keeper_fifo* const _fifo{ new (L_) keeper_fifo{} };
    STACK_CHECK(L_, 1);
    lua_newtable(L_);
    lua_setiuservalue(L_, -2, kContentsTableIndex);
    STACK_CHECK(L_, 1);
    return _fifo;
}

// #################################################################################################

// in: expect fifo ... on top of the stack
// out: nothing, removes all pushed values from the stack
static void fifo_push(lua_State* L_, keeper_fifo* fifo_, int count_)
{
    int const _idx{ lua_gettop(L_) - count_ };
    int const _start{ fifo_->first + fifo_->count - 1 };
    // pop all additional arguments, storing them in the fifo
    for (int const _i : std::ranges::reverse_view{ std::ranges::iota_view{ 1, count_ + 1 } }) {
        // store in the fifo the value at the top of the stack at the specified index, popping it from the stack
        lua_rawseti(L_, _idx, _start + _i);
    }
    fifo_->count += count_;
}

// #################################################################################################

// in: fifo
// out: ...|nothing
// expects exactly 1 value on the stack!
// currently only called with a count of 1, but this may change in the future
// function assumes that there is enough data in the fifo to satisfy the request
static void fifo_peek(lua_State* const L_, keeper_fifo const* const fifo_, int const count_)
{
    STACK_GROW(L_, count_);
    for (int const _i : std::ranges::iota_view{ 0, count_ }) {
        lua_rawgeti(L_, 1, (fifo_->first + _i));
    }
}

// #################################################################################################

// in: fifo
// out: remove the fifo from the stack, push as many items as required on the stack (function assumes they exist in sufficient number)
static void fifo_pop(lua_State* L_, keeper_fifo* fifo_, int count_)
{
    LUA_ASSERT(L_, lua_istable(L_, -1));
    int const _fifo_idx{ lua_gettop(L_) };                                                         // L_: ... fifotbl
    // each iteration pushes a value on the stack!
    STACK_GROW(L_, count_ + 2);
    // skip first item, we will push it last
    for (int const _i : std::ranges::iota_view{ 1, count_ }) {
        int const _at{ fifo_->first + _i };
        // push item on the stack
        lua_rawgeti(L_, _fifo_idx, _at);                                                           // L_: ... fifotbl val
        // remove item from the fifo
        lua_pushnil(L_);                                                                           // L_: ... fifotbl val nil
        lua_rawseti(L_, _fifo_idx, _at);                                                           // L_: ... fifotbl val
    }
    // now process first item
    {
        int const _at{ fifo_->first };
        lua_rawgeti(L_, _fifo_idx, _at);                                                           // L_: ... fifotbl vals val
        lua_pushnil(L_);                                                                           // L_: ... fifotbl vals val nil
        lua_rawseti(L_, _fifo_idx, _at);                                                           // L_: ... fifotbl vals val
        lua_replace(L_, _fifo_idx);                                                                // L_: ... vals
    }

    // avoid ever-growing indexes by resetting each time we detect the fifo is empty
    {
        int const _new_count{ fifo_->count - count_ };
        fifo_->first = (_new_count == 0) ? 1 : (fifo_->first + count_);
        fifo_->count = _new_count;
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
    Keeper* const _K{ linda_.whichKeeper() };
    SourceState const _KL{ _K ? _K->L : nullptr };
    if (_KL == nullptr)
        return 0;
    STACK_GROW(_KL, 4);
    STACK_CHECK_START_REL(_KL, 0);
    kFifosRegKey.pushValue(_KL);                                                                   // KL: fifos                                          L_:
    lua_pushlightuserdata(_KL, &linda_);                                                           // KL: fifos ud                                       L_:
    lua_rawget(_KL, -2);                                                                           // KL: fifos storage                                  L_:
    lua_remove(_KL, -2);                                                                           // KL: storage                                        L_:
    if (!lua_istable(_KL, -1)) {
        lua_pop(_KL, 1);                                                                           // KL:                                                L_:
        STACK_CHECK(_KL, 0);
        return 0;
    }
    // move data from keeper to destination state
    STACK_GROW(L_, 5);
    STACK_CHECK_START_REL(L_, 0);
    lua_newtable(L_);                                                                              // KL: storage                                        L_: out
    InterCopyContext _c{ linda_.U, L_, _KL, {}, {}, {}, LookupMode::FromKeeper, {} };
    lua_pushnil(_KL);                                                                              // KL: storage nil                                    L_: out
    while (lua_next(_KL, -2)) {                                                                    // KL: storage key fifo                               L_: out
        keeper_fifo* fifo = prepare_fifo_access(_KL, -1);                                          // KL: storage key fifotbl                            L_: out
        lua_pushvalue(_KL, -2);                                                                    // KL: storage key fifotbl key                        L_: out
        std::ignore = _c.inter_move(1);                                                            // KL: storage key fifotbl                            L_: out key
        STACK_CHECK(L_, 2);
        lua_newtable(L_);                                                                          // KL: storage key                                    L_: out key keyout
        std::ignore = _c.inter_move(1);                                                            // KL: storage key                                    L_: out key keyout fifotbl
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
    lua_pop(_KL, 1);                                                                               // KL:                                                L_: out
    STACK_CHECK(_KL, 0);
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
    int const _n{ lua_gettop(L_) - 2 };
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
    keeper_fifo* _fifo{ keeper_fifo::getPtr(L_, -1) };
    if (_fifo->limit >= 0 && _fifo->count + _n > _fifo->limit) {
        lua_settop(L_, 0);                                                                         // L_:
        lua_pushboolean(L_, 0);                                                                    // L_:false
    } else {
        _fifo = prepare_fifo_access(L_, -1);                                                       // L_: ud fifotbl
        lua_replace(L_, 2);                                                                        // L_: ud fifotbl ...
        fifo_push(L_, _fifo, _n);                                                                  // L_: ud fifotbl
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
    int const _top{ lua_gettop(L_) };
    push_table(L_, 1);  // L_: ud keys fifos
    lua_replace(L_, 1); // L_: fifos keys
    for (int _i = 2; _i <= _top; ++_i) {
        lua_pushvalue(L_, _i);                                                                     // L_: fifos keys key[i]
        lua_rawget(L_, 1);                                                                         // L_: fifos keys fifo
        keeper_fifo* const _fifo{ prepare_fifo_access(L_, -1) };                                   // L_: fifos keys fifotbl
        if (_fifo != nullptr && _fifo->count > 0) {
            fifo_pop(L_, _fifo, 1);                                                                // L_: fifos keys val
            if (!lua_isnil(L_, -1)) {
                lua_replace(L_, 1);                                                                // L_: val keys
                lua_settop(L_, _i);                                                                // L_: val keys key[i]
                if (_i != 2) {
                    lua_replace(L_, 2);                                                            // L_: val key keys
                    lua_settop(L_, 2);                                                             // L_: val key
                }
                lua_insert(L_, 1);                                                                 // L_: key, val
                return 2;
            }
        }
        lua_settop(L_, _top);                                                                      // L_: data keys
    }
    // nothing to receive
    return 0;
}

// #################################################################################################

// in: linda_ud key mincount [maxcount]
int keepercall_receive_batched(lua_State* L_)
{
    int const _min_count{ static_cast<int>(lua_tointeger(L_, 3)) };
    if (_min_count > 0) {
        int const _max_count{ static_cast<int>(luaL_optinteger(L_, 4, _min_count)) };
        lua_settop(L_, 2);                                                                         // L_: ud key
        lua_insert(L_, 1);                                                                         // L_: key ud
        push_table(L_, 2);                                                                         // L_: key ud fifos
        lua_remove(L_, 2);                                                                         // L_: key fifos
        lua_pushvalue(L_, 1);                                                                      // L_: key fifos key
        lua_rawget(L_, 2);                                                                         // L_: key fifos fifo
        lua_remove(L_, 2);                                                                         // L_: key fifo
        keeper_fifo* const _fifo{ prepare_fifo_access(L_, 2) };                                    // L_: key fifotbl
        if (_fifo != nullptr && _fifo->count >= _min_count) {
            fifo_pop(L_, _fifo, std::min(_max_count, _fifo->count));                               // L_: key ...
        } else {
            lua_settop(L_, 0);                                                                     // L_:
        }
        return lua_gettop(L_);
    } else {
        return 0;
    }
}

// #################################################################################################

// in: linda_ud key [n|nil]
// out: true or nil
int keepercall_limit(lua_State* L_)
{
    int const _limit{ static_cast<int>(luaL_optinteger(L_, 3, -1)) }; // -1 if we read nil because the argument is absent
    push_table(L_, 1);                                                                             // L_: ud key n? fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key n?
    lua_settop(L_, 2);                                                                             // L_: fifos key
    lua_pushvalue(L_, -1);                                                                         // L_: fifos key key
    lua_rawget(L_, -3);                                                                            // L_: fifos key fifo|nil
    keeper_fifo* _fifo{ keeper_fifo::getPtr(L_, -1) };
    if (_fifo == nullptr) {                                                                        // L_: fifos key nil
        lua_pop(L_, 1);                                                                            // L_: fifos key
        _fifo = fifo_new(KeeperState{ L_ });                                                       // L_: fifos key fifo
        lua_rawset(L_, -3);                                                                        // L_: fifos
    }
    // remove any clutter on the stack
    lua_settop(L_, 0);                                                                             // L_:
    // return true if we decide that blocked threads waiting to write on that key should be awakened
    // this is the case if we detect the key was full but it is no longer the case
    if (
        ((_fifo->limit >= 0) && (_fifo->count >= _fifo->limit)) // the key was full if limited and count exceeded the previous limit
        && ((_limit < 0) || (_fifo->count < _limit))            // the key is not full if unlimited or count is lower than the new limit
    ) {
        lua_pushboolean(L_, 1);                                                                    // L_: true
    }
    // set the new limit
    _fifo->limit = _limit;
    // return 0 or 1 value
    return lua_gettop(L_);
}

// #################################################################################################

// in: linda_ud key [[val] ...]
// out: true if the linda was full but it's no longer the case, else nothing
int keepercall_set(lua_State* L_)
{
    bool _should_wake_writers{ false };
    STACK_GROW(L_, 6);

    // retrieve fifos associated with the linda
    push_table(L_, 1);                                                                             // L_: ud key [val [, ...]] fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key [val [, ...]]

    // make sure we have a value on the stack
    if (lua_gettop(L_) == 2) {                                                                     // L_: fifos key
        lua_pushvalue(L_, -1);                                                                     // L_: fifos key key
        lua_rawget(L_, 1);                                                                         // L_: fifos key fifo|nil
        // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
        keeper_fifo* const _fifo{ keeper_fifo::getPtr(L_, -1) };
        if (_fifo != nullptr) { // might be nullptr if we set a nonexistent key to nil             // L_: fifos key fifo
            if (_fifo->limit < 0) {                                                                // fifo limit value is the default (unlimited): we can totally remove it
                lua_pop(L_, 1);                                                                    // L_: fifos key
                lua_pushnil(L_);                                                                   // L_: fifos key nil
                lua_rawset(L_, -3);                                                                // L_: fifos
            } else {
                // we create room if the fifo was full but it is no longer the case
                _should_wake_writers = (_fifo->limit > 0) && (_fifo->count >= _fifo->limit);
                lua_remove(L_, -2);                                                                // L_: fifos fifo
                lua_newtable(L_);                                                                  // L_: fifos fifo {}
                lua_setiuservalue(L_, -2, kContentsTableIndex);                                    // L_: fifos fifo
                _fifo->first = 1;
                _fifo->count = 0;
            }
        }
    } else { // set/replace contents stored at the specified key?
        int const _count{ lua_gettop(L_) - 2 };                                                    // number of items we want to store
        lua_pushvalue(L_, 2);                                                                      // L_: fifos key [val [, ...]] key
        lua_rawget(L_, 1);                                                                         // L_: fifos key [val [, ...]] fifo|nil
        keeper_fifo* _fifo{ keeper_fifo::getPtr(L_, -1) };
        if (_fifo == nullptr) { // can be nullptr if we store a value at a new key                 // fifos key [val [, ...]] nil
            // no need to wake writers in that case, because a writer can't wait on an inexistent key
            lua_pop(L_, 1);                                                                        // L_: fifos key [val [, ...]]
            std::ignore = fifo_new(KeeperState{ L_ });                                             // L_: fifos key [val [, ...]] fifo
            lua_pushvalue(L_, 2);                                                                  // L_: fifos key [val [, ...]] fifo key
            lua_pushvalue(L_, -2);                                                                 // L_: fifos key [val [, ...]] fifo key fifo
            lua_rawset(L_, 1);                                                                     // L_: fifos key [val [, ...]] fifo
        } else {                                                                                   // L_: fifos key [val [, ...]] fifo
            // the fifo exists, we just want to update its contents
            // we create room if the fifo was full but it is no longer the case
            _should_wake_writers = (_fifo->limit > 0) && (_fifo->count >= _fifo->limit) && (_count < _fifo->limit);
            // empty the fifo for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
            lua_newtable(L_);                                                                      // L_: fifos key [val [, ...]] fifo {}
            lua_setiuservalue(L_, -2, kContentsTableIndex);                                        // L_: fifos key [val [, ...]] fifo
            _fifo->first = 1;
            _fifo->count = 0;
        }
        _fifo = prepare_fifo_access(L_, -1);                                                       // L_: fifos key [val [, ...]] fifotbl
        // move the fifo below the values we want to store
        lua_insert(L_, 3);                                                                         // L_: fifos key fifotbl [val [, ...]]
        fifo_push(L_, _fifo, _count);                                                              // L_: fifos key fifotbl
    }
    return _should_wake_writers ? (lua_pushboolean(L_, 1), 1) : 0;
}

// #################################################################################################

// in: linda_ud key [count]
// out: at most <count> values
int keepercall_get(lua_State* L_)
{
    int _count{ 1 };
    if (lua_gettop(L_) == 3) {                                                                     // L_: ud key count
        _count = static_cast<int>(lua_tointeger(L_, 3));
        lua_pop(L_, 1);                                                                            // L_: ud key
    }
    push_table(L_, 1);                                                                             // L_: ud key fifos
    lua_replace(L_, 1);                                                                            // L_: fifos key
    lua_rawget(L_, 1);                                                                             // L_: fifos fifo
    keeper_fifo* const _fifo{ prepare_fifo_access(L_, -1) };                                       // L_: fifos fifotbl
    if (_fifo != nullptr && _fifo->count > 0) {
        lua_remove(L_, 1);                                                                         // L_: fifotbl
        _count = std::min(_count, _fifo->count);
        // read <count> value off the fifo
        fifo_peek(L_, _fifo, _count);                                                              // L_: fifotbl ...
        return _count;
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
            keeper_fifo* const _fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pop(L_, 1);                                                                        // L_: out fifos key
            lua_pushvalue(L_, -1);                                                                 // L_: out fifos key key
            lua_pushinteger(L_, _fifo->count);                                                     // L_: out fifos key key count
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
            keeper_fifo* const _fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pushinteger(L_, _fifo->count);                                                     // L_: fifos fifo count
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
            keeper_fifo* const _fifo{ keeper_fifo::getPtr(L_, -1) };
            lua_pop(L_, 1);                                                                        // L_: out fifos keys...
            if (_fifo != nullptr) {                                                                // L_: the key is known
                lua_pushinteger(L_, _fifo->count);                                                 // L_: out fifos keys... count
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

Keeper* Linda::acquireKeeper() const
{
    int const _nbKeepers{ U->keepers->nb_keepers };
    // can be 0 if this happens during main state shutdown (lanes is being GC'ed -> no keepers)
    if (_nbKeepers) {
        Keeper* const _K{ &U->keepers->keeper_array[keeperIndex] };
        _K->mutex.lock();
        return _K;
    }
    return nullptr;
}

// #################################################################################################

void Linda::releaseKeeper(Keeper* K_) const
{
    if (K_) { // can be nullptr if we tried to acquire during shutdown
        assert(K_ == &U->keepers->keeper_array[keeperIndex]);
        K_->mutex.unlock();
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
KeeperCallResult keeper_call(KeeperState K_, keeper_api_t func_, lua_State* L_, Linda* linda_, int starting_index_)
{
    KeeperCallResult _result;
    int const _args{ starting_index_ ? (lua_gettop(L_) - starting_index_ + 1) : 0 };               // L: ... args...                                  K_:
    int const _top_K{ lua_gettop(K_) };
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, _top_K == 0);

    STACK_GROW(K_, 2);
    PUSH_KEEPER_FUNC(K_, func_);                                                                   // L: ... args...                                  K_: func_
    lua_pushlightuserdata(K_, linda_);                                                             // L: ... args...                                  K_: func_ linda
    if (
        (_args == 0) ||
        (InterCopyContext{ linda_->U, DestState{ K_ }, SourceState{ L_ }, {}, {}, {}, LookupMode::ToKeeper, {} }.inter_copy(_args) == InterCopyResult::Success)
    ) {                                                                                            // L: ... args...                                  K_: func_ linda args...
        lua_call(K_, 1 + _args, LUA_MULTRET);                                                      // L: ... args...                                  K_: result...
        int const retvals{ lua_gettop(K_) - _top_K };
        // note that this can raise a lua error while the keeper state (and its mutex) is acquired
        // this may interrupt a lane, causing the destruction of the underlying OS thread
        // after this, another lane making use of this keeper can get an error code from the mutex-locking function
        // when attempting to grab the mutex again (WINVER <= 0x400 does this, but locks just fine, I don't know about pthread)
        if (
            (retvals == 0) ||
            (InterCopyContext{ linda_->U, DestState{ L_ }, SourceState{ K_ }, {}, {}, {}, LookupMode::FromKeeper, {} }.inter_move(retvals) == InterCopyResult::Success)
        ) {                                                                                        // L: ... args... result...                        K_: result...
            _result.emplace(retvals);
        }
    }
    // whatever happens, restore the stack to where it was at the origin
    lua_settop(K_, _top_K);                                                                        // L: ... args... result...                        K_:

    // don't do this for this particular function, as it is only called during Linda destruction, and we don't want to raise an error, ever
    if (func_ != KEEPER_API(clear)) [[unlikely]] {
        // since keeper state GC is stopped, let's run a step once in a while if required
        int const _gc_threshold{ linda_->U->keepers->gc_threshold };
        if (_gc_threshold == 0) [[unlikely]] {
            lua_gc(K_, LUA_GCSTEP, 0);
        } else if (_gc_threshold > 0) [[likely]] {
            int const _gc_usage{ lua_gc(K_, LUA_GCCOUNT, 0) };
            if (_gc_usage >= _gc_threshold) {
                lua_gc(K_, LUA_GCCOLLECT, 0);
                int const _gc_usage_after{ lua_gc(K_, LUA_GCCOUNT, 0) };
                if (_gc_usage_after > _gc_threshold) [[unlikely]] {
                    raise_luaL_error(L_, "Keeper GC threshold is too low, need at least %d", _gc_usage_after);
                }
            }
        }
    }

    return _result;
}

// #################################################################################################

void Keepers::CreateFifosTable(lua_State* L_)
{
    kFifosRegKey.setValue(L_, [](lua_State* L_) { lua_newtable(L_); });
}
