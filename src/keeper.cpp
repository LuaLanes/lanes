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
#include "_pch.hpp"
#include "keeper.hpp"

#include "intercopycontext.hpp"
#include "lane.hpp"
#include "linda.hpp"
#include "state.hpp"

// There is a table at _R[kLindasRegKey] (aka LindasDB)
// This table contains entries of the form [Linda*] = {KeysDB...}
// Each KeysDB contains entries of the form [key] = KeyUD
// where key is a key used in the Lua Linda API to exchange data, and KeyUD is a full userdata with a table uservalue
// the table uservalue is the actual fifo, where elements are added and removed.

namespace {

// #################################################################################################
// #################################################################################################
// ############################################ KeyUD ##############################################
// #################################################################################################
// #################################################################################################

// the full userdata associated to a given Linda key to store its contents
class KeyUD final
{
    private:
    static constexpr UserValueIndex kContentsTableIndex{ 1 };

    public:
    static constexpr std::string_view kUnder{ "under" };
    static constexpr std::string_view kExact{ "exact" };
    static constexpr std::string_view kOver{ "over" };

    int first{ 1 };
    int count{ 0 };
    LindaLimit limit{ -1 };
    LindaRestrict restrict { LindaRestrict::None };

    // a fifo full userdata has one uservalue, the table that holds the actual fifo contents
    [[nodiscard]]
    static void* operator new([[maybe_unused]] size_t size_, KeeperState L_) noexcept { return luaW_newuserdatauv<KeyUD>(L_, UserValueCount{ 1 }); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete([[maybe_unused]] void* p_, [[maybe_unused]] KeeperState L_) { LUA_ASSERT(L_, !"should never be called"); }

    [[nodiscard]]
    bool changeLimit(LindaLimit limit_);
    [[nodiscard]]
    LindaRestrict changeRestrict(LindaRestrict restrict_);
    [[nodiscard]]
    static KeyUD* Create(KeeperState K_);
    [[nodiscard]]
    static KeyUD* GetPtr(KeeperState K_, StackIndex idx_);
    void peek(KeeperState K_, int count_) const; // keepercall_get
    [[nodiscard]]
    int pop(KeeperState K_, int minCount_, int maxCount_); // keepercall_receive[_batched]
    void prepareAccess(KeeperState K_, StackIndex idx_) const;
    [[nodiscard]]
    bool push(KeeperState K_, int count_, bool enforceLimit_); // keepercall_send and keepercall_set
    void pushFillStatus(KeeperState K_) const;
    static void PushFillStatus(KeeperState K_, KeyUD const* key_);
    [[nodiscard]]
    bool reset(KeeperState K_);
};

// #################################################################################################

[[nodiscard]]
bool KeyUD::changeLimit(LindaLimit const limit_)
{
    bool const _newSlackAvailable{
        ((limit >= 0) && (count >= limit)) // then: the key was full if limited and count exceeded the previous limit
        && ((limit_ < 0) || (count < limit_)) // now: the key is not full if unlimited or count is lower than the new limit
    };
    // set the new limit
    limit = limit_;
    return _newSlackAvailable;
}

// #################################################################################################

[[nodiscard]]
LindaRestrict KeyUD::changeRestrict(LindaRestrict const restrict_)
{
    return std::exchange(restrict, restrict_);
}

// #################################################################################################

// in: nothing
// out: { first = 1, count = 0, limit = -1}
[[nodiscard]]
KeyUD* KeyUD::Create(KeeperState const K_)
{
    STACK_GROW(K_, 2);
    STACK_CHECK_START_REL(K_, 0);
    KeyUD* const _key{ new (K_) KeyUD{} };
    STACK_CHECK(K_, 1);
    lua_newtable(K_);
    lua_setiuservalue(K_, StackIndex{ -2 }, kContentsTableIndex);
    STACK_CHECK(K_, 1);
    return _key;
}

// #################################################################################################

[[nodiscard]]
KeyUD* KeyUD::GetPtr(KeeperState const K_, StackIndex const idx_)
{
    return luaW_tofulluserdata<KeyUD>(K_, idx_);
}

// #################################################################################################

// in: fifo
// out: bool ...
// pops the fifo, push bool + as much data as is available (up to the specified count) without consuming it
// bool is true if the requested count was served, else false
void KeyUD::peek(KeeperState const K_, int const count_) const
{
    STACK_CHECK_START_ABS(K_, 1);
    LUA_ASSERT(K_, KeyUD::GetPtr(K_, kIdxTop) == this);                                            // K_: KeyUD
    if (count <= 0) { // no data is available
        lua_pop(K_, 1);                                                                            // K_:
        lua_pushinteger(K_, 0);                                                                    // K_: 0
        return;
    }

    // read <count_> value off the fifo, if possible
    prepareAccess(K_, kIdxTop);                                                                    // K_: fifo
    int const _count{ std::min(count_, count) };
    lua_pushinteger(K_, _count);                                                                   // K_: fifo _count
    lua_insert(K_, 1);                                                                             // K_: _count fifo
    STACK_CHECK(K_, 2);
    STACK_GROW(K_, _count);
    for (int const _i : std::ranges::iota_view{ 1, _count }) { // push val2 to valN
        lua_rawgeti(K_, 2, first + _i);                                                            // K_: _count fifo val2..N
    }
    lua_rawgeti(K_, 2, first); // push val1                                                        // K_: _count fifo val2..N val1
    lua_replace(K_, 2); // replace fifo by val1 to get the output properly ordered                 // K_: _count val1..N
    STACK_CHECK(K_, 1 + _count);
}

// #################################################################################################

// in: fifo
// out: remove the fifo table from the stack, push as many items as required on the stack (function assumes they exist in sufficient number)
[[nodiscard]]
int KeyUD::pop(KeeperState const K_, int const minCount_, int const maxCount_)
{
    if (count < minCount_) {
        // pop ourselves, return nothing
        lua_pop(K_, 1);                                                                            // K_: ... this
        return 0;
    }
    int const _popCount{ std::min(count, maxCount_) };
    LUA_ASSERT(K_, KeyUD::GetPtr(K_, kIdxTop) == this);                                            // K_: ... this
    prepareAccess(K_, kIdxTop);                                                                    // K_: ... fifo

    STACK_CHECK_START_REL(K_, 0);
    StackIndex const _fifo_idx{ lua_gettop(K_) };
    // each iteration pushes a value on the stack!
    STACK_GROW(K_, _popCount + 2);

    // remove an element from fifo sequence and push it on the stack
    auto _extractFifoItem = [K = K_, first = first, fifo_idx = lua_gettop(K_)](int const _i)
    {
        STACK_CHECK_START_REL(K, 0);
        int const _at{ first + _i };
        // push item on the stack
        lua_rawgeti(K, fifo_idx, _at);                                                             // K_: ... fifo val
        // remove item from the fifo
        lua_pushnil(K);                                                                            // K_: ... fifo val nil
        lua_rawseti(K, fifo_idx, _at);                                                             // K_: ... fifo val
        STACK_CHECK(K, 1);
    };

    // skip first item, we will push it last to avoid shifting the whole stack when removing 'fifo'
    for (int const _i : std::ranges::iota_view{ 1, _popCount }) {
        _extractFifoItem(_i);                                                                      // K_: ... fifo val1...valN
    }
    // now process first item
    _extractFifoItem(0);                                                                           // K_: ... fifo val1...valN val0
    STACK_CHECK(K_, _popCount);
    lua_replace(K_, _fifo_idx);                                                                    // K_: ... val0...valN

    // avoid ever-growing indexes by resetting each time we detect the fifo is empty
    int const _new_count{ count - _popCount };
    first = (_new_count == 0) ? 1 : (first + _popCount);
    count = _new_count;
    return _popCount;
}

// #################################################################################################

// expects 'this' at the specified index
// replaces it by its uservalue on the stack (the table holding the fifo values)
void KeyUD::prepareAccess(KeeperState const K_, StackIndex const idx_) const
{
    StackIndex const _idx{ luaW_absindex(K_, idx_) };
    LUA_ASSERT(K_, KeyUD::GetPtr(K_, idx_) == this);
    // we can replace the key userdata in the stack without fear of it being GCed, there are other references around
    lua_getiuservalue(K_, _idx, kContentsTableIndex);
    lua_replace(K_, _idx);
}

// #################################################################################################

// in: expect this val... on top of the stack
// out: nothing, removes all pushed values from the stack
[[nodiscard]]
bool KeyUD::push(KeeperState const K_, int const count_, bool const enforceLimit_)
{
    StackIndex const _fifoIdx{ luaW_absindex(K_, StackIndex{ -1 - count_ }) };
    LUA_ASSERT(K_, KeyUD::GetPtr(K_, _fifoIdx) == this);                                           // K_: this val...
    if (enforceLimit_ && (limit >= 0) && (count + count_ > limit)) { // not enough room
        return false;
    }

    prepareAccess(K_, _fifoIdx);                                                                   // K_: fifo val...
    int const _start{ first + count - 1 };
    // pop all additional arguments, storing them in the fifo
    for (int const _i : std::ranges::reverse_view{ std::ranges::iota_view{ 1, count_ + 1 } }) {
        // store in the fifo the value at the top of the stack at the specified index, popping it from the stack
        lua_rawseti(K_, _fifoIdx, _start + _i);
    }
    count += count_;
    // all values are, gone, only our fifo remains, we can remove it
    lua_pop(K_, 1);                                                                                // K_:
    return true;
}

// #################################################################################################

void KeyUD::pushFillStatus(KeeperState const K_) const
{
    if (limit < 0) {
        luaW_pushstring(K_, kUnder);
        return;
    }
    int const _delta{ limit - count };
    if (_delta < 0) {
        luaW_pushstring(K_, kOver);
    } else if (_delta > 0) {
        luaW_pushstring(K_, kUnder);
    } else {
        luaW_pushstring(K_, kExact);
    }
}

// #################################################################################################

void KeyUD::PushFillStatus(KeeperState const K_, KeyUD const* const key_)
{
    if (key_) {
        key_->pushFillStatus(K_);                                                                  // _K: ... <fill status>
    } else {
        luaW_pushstring(K_, KeyUD::kUnder);                                                        // _K: ... "under"
    }
}

// #################################################################################################

// in: expects 'this' on top of the stack
// out: nothing
// returns true if the channel was full
[[nodiscard]]
bool KeyUD::reset(KeeperState const K_)
{
    LUA_ASSERT(K_, KeyUD::GetPtr(K_, kIdxTop) == this);
    STACK_CHECK_START_REL(K_, 0);
    bool const _wasFull{ (limit > 0) && (count >= limit) };
    // empty the KeyUD: replace uservalue with a virgin table, reset counters, but leave limit and restrict unchanged!
    // if we have an actual limit, use it to preconfigure the table
    lua_createtable(K_, (limit <= 0) ? 0 : limit.value(), 0);                                      // K_: KeysDB key val... KeyUD {}
    lua_setiuservalue(K_, StackIndex{ -2 }, kContentsTableIndex);                                  // K_: KeysDB key val... KeyUD
    first = 1;
    count = 0;
    STACK_CHECK(K_, 0);
    return _wasFull;
}

// #################################################################################################
// #################################################################################################

// in: linda_ud expected at stack slot idx
// out: the KeysDB table of the linda is pushed at the top of the stack
// xxh64 of string "kLindasRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLindasRegKey{ 0x3AE0D5243A88B962ull };
static void PushKeysDB(KeeperState const K_, StackIndex const idx_)
{
    STACK_GROW(K_, 5);
    STACK_CHECK_START_REL(K_, 0);
    StackIndex const _absidx{ luaW_absindex(K_, idx_) };
    kLindasRegKey.pushValue(K_);                                                                   // K_: ... LindasDB
    lua_pushvalue(K_, _absidx);                                                                    // K_: ... LindasDB linda
    if (luaW_rawget(K_, StackIndex{ -2 }) == LuaType::NIL) {                                       // K_: ... LindasDB KeysDB
        lua_pop(K_, 1);                                                                            // K_: ... LindasDB
        // add a new KeysDB table for this linda
        lua_newtable(K_);                                                                          // K_: ... LindasDB KeysDB
        lua_pushvalue(K_, _absidx);                                                                // K_: ... LindasDB KeysDB linda
        lua_pushvalue(K_, -2);                                                                     // K_: ... LindasDB KeysDB linda KeysDB
        lua_rawset(K_, -4);                                                                        // K_: ... LindasDB KeysDB
    }
    lua_remove(K_, -2);                                                                            // K_: ... KeysDB
    STACK_CHECK(K_, 1);
}

} // namespace

// #################################################################################################
// #################################################################################################
// ######################################## keepercall_XXX #########################################
// #################################################################################################
// #################################################################################################

// in: linda
// out: nothing
[[nodiscard]]
int keepercall_collectgarbage(lua_State* const L_)
{
    lua_gc(L_, LUA_GCCOLLECT, 0);
    return 0;
}

// #################################################################################################

// in: linda [, key [, ...]]
[[nodiscard]]
int keepercall_count(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    switch (lua_gettop(_K)) {
    // no key is specified: return a table giving the count of all known keys
    case 1:                                                                                        // _K: linda
        PushKeysDB(_K, StackIndex{ 1 });                                                           // _K: linda KeysDB
        lua_newtable(_K);                                                                          // _K: linda KeysDB out
        lua_replace(_K, 1);                                                                        // _K: out KeysDB
        lua_pushnil(_K);                                                                           // _K: out KeysDB nil
        while (lua_next(_K, 2)) {                                                                  // _K: out KeysDB key KeyUD
            KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
            lua_pop(_K, 1);                                                                        // _K: out KeysDB key
            lua_pushvalue(_K, -1);                                                                 // _K: out KeysDB key key
            lua_pushinteger(_K, _key->count);                                                      // _K: out KeysDB key key count
            lua_rawset(_K, -5);                                                                    // _K: out KeysDB key
        } // when loop is done, lua_next() pushes nothing                                          // _K: out KeysDB
        lua_pop(_K, 1);                                                                            // _K: out
        break;

    // 1 key is specified: return its count
    case 2:                                                                                        // _K: linda key
        PushKeysDB(_K, StackIndex{ 1 });                                                           // _K: linda key KeysDB
        lua_replace(_K, 1);                                                                        // _K: KeysDB key
        if (luaW_rawget(_K, StackIndex{ -2 }) == LuaType::NIL) { // the key is unknown             // _K: KeysDB KeyUD|nil
            lua_remove(_K, -2);                                                                    // _K: nil
        } else { // the key is known                                                               // _K: KeysDB KeyUD
            KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
            lua_pushinteger(_K, _key->count);                                                      // _K: KeysDB KeyUD count
            lua_replace(_K, -3);                                                                   // _K: count KeyUD
            lua_pop(_K, 1);                                                                        // _K: count
        }
        break;

    // a variable number of keys is specified: return a table of their counts
    default:                                                                                       // _K: linda keys... key#1
        lua_pushvalue(_K, 2); // duplicate the first key of the list                               // _K: linda keys... key#1
        PushKeysDB(_K, StackIndex{ 1 });                                                           // _K: linda keys... key#1 KeysDB
        lua_newtable(_K);                                                                          // _K: linda keys... key#1 KeysDB out
        lua_replace(_K, 1);                                                                        // _K: out keys... key#1 KeysDB
        lua_replace(_K, 2); // the list of keys is the same, but for key#1 moved at the end        // _K: out KeysDB keys...
        while (lua_gettop(_K) > 2) {
            lua_pushvalue(_K, -1);                                                                 // _K: out KeysDB keys... key
            lua_rawget(_K, 2);                                                                     // _K: out KeysDB keys... KeyUD|nil
            KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
            lua_pop(_K, 1);                                                                        // _K: out KeysDB keys...
            if (_key != nullptr) { // the key is known
                lua_pushinteger(_K, _key->count);                                                  // _K: out KeysDB keys... count
                lua_rawset(_K, 1);                                                                 // _K: out KeysDB keys...
            } else { // the key is unknown
                lua_pop(_K, 1);                                                                    // _K: out KeysDB keys...
            }
        } // all keys are exhausted                                                                // _K: out KeysDB
        lua_pop(_K, 1);                                                                            // _K: out
    }
    LUA_ASSERT(_K, lua_gettop(_K) == 1);
    return 1;
}

// #################################################################################################

// in: linda
// not part of the linda public API, only used for cleanup at linda GC
[[nodiscard]]
int keepercall_destruct(lua_State* const L_)
{
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    // LindasDB[linda] = nil
    kLindasRegKey.pushValue(L_);                                                                   // L_: linda LindasDB
    lua_pushvalue(L_, 1);                                                                          // L_: linda LindasDB linda
    lua_pushnil(L_);                                                                               // L_: linda LindasDB linda nil
    lua_rawset(L_, -3);                                                                            // L_: linda LindasDB
    lua_pop(L_, 1);                                                                                // L_: linda
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

// in: linda_ud key [count]
// out: N <N values>|kRestrictedChannel
[[nodiscard]]
int keepercall_get(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    int _count{ 1 };
    if (lua_gettop(_K) == 3) {                                                                     // _K: linda key count
        _count = static_cast<int>(lua_tointeger(_K, 3)); // linda:get() made sure _count >= 1
        lua_pop(_K, 1);                                                                            // _K: linda key
    }
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda key KeysDB
    lua_replace(_K, 1);                                                                            // _K: KeysDB key
    lua_rawget(_K, 1);                                                                             // _K: KeysDB KeyUD
    lua_remove(_K, 1);                                                                             // _K: KeyUD
    KeyUD const* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
    if (_key != nullptr) {
        if (_key->restrict == LindaRestrict::SendReceive) { // can we use set/get?
            lua_settop(_K, 0);                                                                     // _K:
            kRestrictedChannel.pushKey(_K);                                                        // _K: kRestrictedChannel
            return 1;
        } else {
            _key->peek(_K, _count);                                                                // _K: N val...
        }
    } else {
        // no fifo was ever registered for this key, or it is empty
        lua_pop(_K, 1);                                                                            // _K:
        lua_pushinteger(_K, 0);                                                                    // _K: 0
    }
    LUA_ASSERT(_K, lua_isnumber(_K, 1));
    return lua_gettop(_K);
}

// #################################################################################################

// in: linda key [n|nil]
// out: boolean, <fill status: string>
[[nodiscard]]
int keepercall_limit(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    STACK_CHECK_START_ABS(_K, lua_gettop(_K));
    // no limit to set, means we read and return the current limit instead
    bool const _reading{ lua_gettop(_K) == 2 };
    LindaLimit const _limit{ static_cast<LindaLimit::type>(luaL_optinteger(_K, 3, -1)) }; // -1 if we read nil because the argument is absent
    lua_settop(_K, 2);                                                                             // _K: linda key
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda key KeysDB
    lua_replace(_K, 1);                                                                            // _K: KeysDB key
    lua_pushvalue(_K, -1);                                                                         // _K: KeysDB key key
    lua_rawget(_K, -3);                                                                            // _K: KeysDB key KeyUD|nil
    KeyUD* _key{ KeyUD::GetPtr(_K, kIdxTop) };
    if (_reading) {
        // remove any clutter on the stack
        lua_settop(_K, 0);                                                                         // _K:
        if (_key && _key->limit >= 0) {
            lua_pushinteger(_K, _key->limit);                                                      // _K: limit
        } else { // if the key doesn't exist, it is unlimited by default
            luaW_pushstring(_K, "unlimited");                                                      // _K: "unlimited"
        }
        // return a single value: the limit of the key
    } else {
        if (_key == nullptr) {                                                                     // _K: KeysDB key nil
            lua_pop(_K, 1);                                                                        // _K: KeysDB key
            _key = KeyUD::Create(_K);                                                              // _K: KeysDB key KeyUD
            lua_rawset(_K, -3);                                                                    // _K: KeysDB
        }
        // remove any clutter on the stack
        lua_settop(_K, 0);                                                                         // _K:
        // return true if we decide that blocked threads waiting to write on that key should be awakened
        // this is the case if we detect the key was full but it is no longer the case
        lua_pushboolean(_K, _key->changeLimit(_limit) ? 1 : 0);                                    // _K: bool
    }
    KeyUD::PushFillStatus(_K, _key);                                                               // _K: limit|bool <fill status>
    STACK_CHECK(_K, 2);
    return 2;
}

// #################################################################################################

// in: linda, key [, key]?
// out: (key, val) or nothing
[[nodiscard]]
int keepercall_receive(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    StackIndex const _top{ lua_gettop(_K) };
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda keys... KeysDB
    lua_replace(_K, 1);                                                                            // _K: KeysDB keys...
    
    for (StackIndex const _keyIdx : std::ranges::iota_view{ StackIndex{ 2 }, _top + 1 }) {
        lua_pushvalue(_K, _keyIdx);                                                                // _K: KeysDB keys... key[i]
        lua_rawget(_K, 1);                                                                         // _K: KeysDB keys... KeyUD
        KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
        if (_key != nullptr) { // it's fine to attempt a read on a key that wasn't yet written to
            if (_key->restrict == LindaRestrict::SetGet) { // can we use send/receive?
                kRestrictedChannel.pushKey(_K);                                                    // _K: KeysDB keys... key[i] kRestrictedChannel
                lua_replace(_K, 1);                                                                // _K: kRestrictedChannel keys... key[i]
                lua_settop(_K, _keyIdx);                                                           // _K: kRestrictedChannel keys... key[i]
                if (_keyIdx != 2) {
                    lua_replace(_K, 2);                                                            // _K: kRestrictedChannel key[i] keys...
                    lua_settop(_K, 2);                                                             // _K: kRestrictedChannel key[i]
                }
                lua_insert(_K, 1);                                                                 // _K: key kRestrictedChannel
                return 2;
            }
            int const _popped{ _key->pop(_K, 1, 1) };                                              // _K: KeysDB keys... val
            if (_popped > 0) {
                lua_replace(_K, 1);                                                                // _K: val keys...
                lua_settop(_K, _keyIdx);                                                           // _K: val keys... key[i]
                if (_keyIdx != 2) {
                    lua_replace(_K, 2);                                                            // _K: val key[i] keys...
                    lua_settop(_K, 2);                                                             // _K: val key[i]
                }
                lua_insert(_K, 1);                                                                 // _K: key val
                return 2;
            }
        }
        lua_settop(_K, _top);                                                                      // _K: data keys...
    }
    // nothing to receive
    return 0;
}

// #################################################################################################

// in: linda key mincount [maxcount]
[[nodiscard]]
int keepercall_receive_batched(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    // linda:receive() made sure that _min_count > 0 and _max_count > _min_count
    int const _min_count{ static_cast<int>(lua_tointeger(_K, 3)) };
    int const _max_count{ static_cast<int>(luaL_optinteger(_K, 4, _min_count)) };
    lua_settop(_K, 2);                                                                             // _K: linda key
    lua_insert(_K, 1);                                                                             // _K: key linda
    PushKeysDB(_K, StackIndex{ 2 });                                                               // _K: key linda KeysDB
    lua_remove(_K, 2);                                                                             // _K: key KeysDB
    lua_pushvalue(_K, 1);                                                                          // _K: key KeysDB key
    lua_rawget(_K, 2);                                                                             // _K: key KeysDB KeyUD
    lua_remove(_K, 2);                                                                             // _K: key KeyUD
    KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
    if (!_key) {
        return 0; // Lua will adjust the stack for us when we return
    }
    if (_key->restrict == LindaRestrict::SetGet) { // can we use send/receive?
        lua_settop(_K, 1);                                                                         // _K: key
        kRestrictedChannel.pushKey(_K);                                                            // _K: key kRestrictedChannel
        return 2;
    }
    if (_key->pop(_K, _min_count, _max_count) == 0) {                                              // _K: [key val...]|crap
        return 0; // Lua will adjust the stack for us when we return
    }
    // return whatever remains on the stack at that point: the key and the values we pulled from the fifo
    return lua_gettop(_K);
}

// #################################################################################################

// in: linda key [mode]
// out: mode
[[nodiscard]]
int keepercall_restrict(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    STACK_CHECK_START_ABS(_K, lua_gettop(_K));
    // no restriction to set, means we read and return the current restriction instead
    bool const _reading{ lua_gettop(_K) == 2 };
    auto _decodeRestrict = [_K, _reading]() {
        if (_reading) {
            return LindaRestrict::None;
        }
        std::string_view const _val{ luaW_tostring(_K, StackIndex{ 3 }) };
        if (_val == "set/get") {
            return LindaRestrict::SetGet;
        }
        if (_val == "send/receive") {
            return LindaRestrict::SendReceive;
        }
        return LindaRestrict::None;
    };
    auto _encodeRestrict = [](LindaRestrict const val_) {
        switch (val_) {
        default:
        case LindaRestrict::None:
            return std::string_view{ "none" };
        case LindaRestrict::SetGet:
            return std::string_view{ "set/get" };
        case LindaRestrict::SendReceive:
            return std::string_view{ "send/receive" };
        }
    };
    LindaRestrict const _rstrct{ _decodeRestrict() }; // if we read nil because the argument is absent
    lua_settop(_K, 2);                                                                             // _K: linda key
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda key KeysDB
    lua_replace(_K, 1);                                                                            // _K: KeysDB key
    lua_pushvalue(_K, -1);                                                                         // _K: KeysDB key key
    lua_rawget(_K, -3);                                                                            // _K: KeysDB key KeyUD|nil
    KeyUD* _key{ KeyUD::GetPtr(_K, kIdxTop) };
    if (_reading) {
        // remove any clutter on the stack
        lua_settop(_K, 0);                                                                         // _K:
        auto const _prevRstrct{ _key ? _key->restrict : LindaRestrict::None };
        // return a single value: the restrict mode of the key
        luaW_pushstring(_K, _encodeRestrict(_prevRstrct));                                          // _K: _previous
    } else {
        if (_key == nullptr) {                                                                     // _K: KeysDB key nil
            lua_pop(_K, 1);                                                                        // _K: KeysDB key
            _key = KeyUD::Create(_K);                                                              // _K: KeysDB key KeyUD
            lua_rawset(_K, -3);                                                                    // _K: KeysDB
        }
        // remove any clutter on the stack
        lua_settop(_K, 0);                                                                         // _K:
        // return true if we decide that blocked threads waiting to write on that key should be awakened
        // this is the case if we detect the key was full but it is no longer the case
        LindaRestrict const _previous{ _key->changeRestrict(_rstrct) };
        luaW_pushstring(_K, _encodeRestrict(_previous));                                           // _K: _previous
    }
    STACK_CHECK(_K, 1);
    return 1;
}

// #################################################################################################

// in: linda, key, ...
// out: true|false|kRestrictedChannel
[[nodiscard]]
int keepercall_send(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    int const _n{ lua_gettop(_K) - 2 };
    STACK_CHECK_START_REL(_K, 0);                                                                  // _K: linda key val...
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda key val... KeysDB
    // get the fifo associated to this key in this linda, create it if it doesn't exist
    lua_pushvalue(_K, 2);                                                                          // _K: linda key val... KeysDB key
    if (luaW_rawget(_K, StackIndex{ -2 }) == LuaType::NIL) {                                       // _K: linda key val... KeysDB KeyUD|nil
        lua_pop(_K, 1);                                                                            // _K: linda key val... KeysDB
        std::ignore = KeyUD::Create(KeeperState{ _K });                                            // _K: linda key val... KeysDB KeyUD
        // KeysDB[key] = KeyUD
        lua_pushvalue(_K, 2);                                                                      // _K: linda key val... KeysDB KeyUD key
        lua_pushvalue(_K, -2);                                                                     // _K: linda key val... KeysDB KeyUD key KeyUD
        lua_rawset(_K, -4);                                                                        // _K: linda key val... KeysDB KeyUD
    }
    lua_replace(_K, 2);                                                                            // _K: linda KeyUD val... KeysDB
    lua_pop(_K, 1);                                                                                // _K: linda KeyUD val...
    STACK_CHECK(_K, 0);
    KeyUD* const _key{ KeyUD::GetPtr(_K, StackIndex{ 2 }) };
    if (_key->restrict == LindaRestrict::SetGet) { // can we use send/receive?
        lua_settop(_K, 0);                                                                         // _K:
        kRestrictedChannel.pushKey(_K);                                                            // _K: kRestrictedChannel
    }
    else if (_key->push(_K, _n, true)) { // not enough room?
        lua_settop(_K, 0);                                                                         // _K:
        lua_pushboolean(_K, 1);                                                                    // _K: true
    } else {
        // don't send anything
        lua_settop(_K, 0);                                                                         // _K:
        lua_pushboolean(_K, 0);                                                                    // _K: false
    }
    return 1;
}

// #################################################################################################

// in: linda key [val...]
// out: true if the linda was full but it's no longer the case, else false, or kRestrictedChannel if the key is restricted
[[nodiscard]]
int keepercall_set(lua_State* const L_)
{
    KeeperState const _K{ L_ };
    bool _should_wake_writers{ false };
    STACK_GROW(_K, 6);

    // retrieve KeysDB associated with the linda
    PushKeysDB(_K, StackIndex{ 1 });                                                               // _K: linda key val... KeysDB
    lua_replace(_K, 1);                                                                            // _K: KeysDB key val...
    lua_pushvalue(_K, 2);                                                                          // _K: KeysDB key val... key
    lua_rawget(_K, 1);                                                                             // _K: KeysDB key val KeyUD|nil
    KeyUD* _key{ KeyUD::GetPtr(_K, kIdxTop) };
    if (_key && _key->restrict == LindaRestrict::SendReceive) { // can we use send/receive?
        lua_settop(_K, 0);                                                                         // _K:
        kRestrictedChannel.pushKey(_K);                                                            // _K: kRestrictedChannel
        return 1;
    }

    if (lua_gettop(_K) == 3) { // no value to set                                                  // _K: KeysDB key KeyUD|nil
        // empty the KeyUD for the specified key: replace uservalue with a virgin table, reset counters, but leave limit unchanged!
        if (_key != nullptr) { // might be nullptr if we set a nonexistent key to nil              // _K: KeysDB key KeyUD
            if (_key->limit < 0 && _key->restrict == LindaRestrict::None) { // KeyUD limit value and restrict mode are the default (unlimited/none): we can totally remove it
                lua_pop(_K, 1);                                                                    // _K: KeysDB key
                lua_pushnil(_K);                                                                   // _K: KeysDB key nil
                lua_rawset(_K, -3);                                                                // _K: KeysDB
            } else {
                lua_remove(_K, -2); // KeyUD::reset expects KeyUD at the top                       // _K: KeysDB KeyUD
                // we create room if the KeyUD was full but it is no longer the case
                _should_wake_writers = _key->reset(_K);
            }
        }
        lua_settop(_K, 0); // we are done, remove everything                                       // _K:
    } else { // set/replace contents stored at the specified key?
        int const _count{ lua_gettop(_K) - 3 };  // number of items we want to store               // _K: KeysDB key val... KeyUD|nil
        if (_key == nullptr) { // can be nullptr if we store a value at a new key                  // _K: KeysDB key val... nil
            assert(lua_isnil(_K, -1));
            lua_pop(_K, 1);                                                                        // _K: KeysDB key val...
            _key = KeyUD::Create(KeeperState{ _K });                                               // _K: KeysDB key val... KeyUD
            lua_pushvalue(_K, 2);                                                                  // _K: KeysDB key val... KeyUD key
            lua_pushvalue(_K, -2);                                                                 // _K: KeysDB key val... KeyUD key KeyUD
            lua_rawset(_K, 1);                                                                     // _K: KeysDB key val... KeyUD
            // no need to wake writers, because a writer can't wait on an inexistent key
        } else {                                                                                   // _K: KeysDB key val... KeyUD
            // the KeyUD exists, we just want to update its contents
            // we create room if the KeyUD was full but we didn't refill it to the brim with new data
            _should_wake_writers = _key->reset(_K) && (_count < _key->limit);
        }
        // replace the key with the KeyUD in the stack
        lua_replace(_K, -2 - _count);                                                              // _K: KeysDB KeyUD val...
        [[maybe_unused]] bool const _pushed{ _key->push(_K, _count, false) };                      // _K: KeysDB
        lua_pop(_K, 1);                                                                            // _K:
    }
    assert(lua_gettop(_K) == 0);
    lua_pushboolean(_K, _should_wake_writers ? 1 : 0);                                             // _K: bool
    KeyUD::PushFillStatus(_K, _key);                                                               // _K: bool <fill status>
    return 2;
}

// #################################################################################################

/*
 * Call a function ('func_name') in the keeper state, and pass on the returned
 * values to 'L'.
 *
 * 'linda':          deep Linda pointer (used only as a unique table key, first argument)
 * 'starting_index': first of the rest of arguments (none if 0)
 *
 * Returns: number of return values (pushed to 'L'), unset in case of error
 */
[[nodiscard]]
KeeperCallResult keeper_call(KeeperState const K_, keeper_api_t const func_, lua_State* const L_, Linda* const linda_, StackIndex const starting_index_)
{
    KeeperCallResult _result;
    StackIndex const _args{ starting_index_ ? (lua_gettop(L_) - starting_index_ + 1) : 0 };        // L: ... args...                                  K_:
    StackIndex const _top_K{ lua_gettop(K_) };
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, _top_K == 0);

    STACK_GROW(K_, 2);
    lua_pushcclosure(K_, func_, 0);                                                                // L: ... args...                                  K_: func_
    lua_pushlightuserdata(K_, linda_);                                                             // L: ... args...                                  K_: func_ linda
    if (
        (_args == 0) ||
        (InterCopyContext{ linda_->U, DestState{ K_.value() }, SourceState{ L_ }, {}, {}, {}, LookupMode::ToKeeper, {} }.interCopy(_args) == InterCopyResult::Success)
    ) {                                                                                            // L: ... args...                                  K_: func_ linda args...
        lua_call(K_, 1 + _args, LUA_MULTRET);                                                      // L: ... args...                                  K_: result...
        int const _retvals{ lua_gettop(K_) - _top_K };
        // note that this can raise a lua error while the keeper state (and its mutex) is acquired
        // this may interrupt a lane, causing the destruction of the underlying OS thread
        // after this, another lane making use of this keeper can get an error code from the mutex-locking function
        // when attempting to grab the mutex again (WINVER <= 0x400 does this, but locks just fine, I don't know about pthread)
        if (
            (_retvals == 0) ||
            (InterCopyContext{ linda_->U, DestState{ L_ }, SourceState{ K_.value() }, {}, {}, {}, LookupMode::FromKeeper, {} }.interMove(_retvals) == InterCopyResult::Success)
        ) {                                                                                        // L: ... args... result...                        K_: result...
            _result.emplace(_retvals);
        }
    }
    // whatever happens, restore the stack to where it was at the origin
    lua_settop(K_, _top_K);                                                                        // L: ... args... result...                        K_:

    // don't do this for this particular function, as it is only called during Linda destruction, and we don't want to raise an error, ever
    if (func_ != KEEPER_API(destruct)) [[unlikely]] {
        // since keeper state GC is stopped, let's run a step once in a while if required
        int const _gc_threshold{ linda_->U->keepers.gc_threshold };
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
// #################################################################################################
// ########################################## Keeper ###############################################
// #################################################################################################
// #################################################################################################

// only used by linda:dump() and linda:__towatch() for debugging purposes
// table is populated as follows:
// {
//     [<key>] = {
//         first = <n>,
//         count = <n>,
//         limit = <n> | 'unlimited',
//         fifo = { <array of values> }
//     }
//     ...
// }
[[nodiscard]]
int Keeper::PushLindaStorage(Linda& linda_, DestState const L_)
{
    Keeper* const _keeper{ linda_.whichKeeper() };
    KeeperState const _K{ _keeper ? _keeper->K : KeeperState{ static_cast<lua_State*>(nullptr) } };
    if (_K == nullptr) {
        return 0;
    }
    STACK_GROW(_K, 4);
    STACK_CHECK_START_REL(_K, 0);
    kLindasRegKey.pushValue(_K);                                                                   // _K: LindasDB                                       L_:
    lua_pushlightuserdata(_K, &linda_);                                                            // _K: LindasDB linda                                 L_:
    LuaType const _type{ luaW_rawget(_K, StackIndex{ -2 }) };                                      // _K: LindasDB KeysDB                                L_:
    lua_remove(_K, -2);                                                                            // _K: KeysDB                                         L_:
    if (_type != LuaType::TABLE) { // possible if we didn't send anything through that linda
        lua_pop(_K, 1);                                                                            // _K:                                                L_:
        STACK_CHECK(_K, 0);
        return 0;
    }
    // move data from keeper to destination state
    STACK_GROW(L_, 5);
    STACK_CHECK_START_REL(L_, 0);
    lua_newtable(L_);                                                                              // _K: KeysDB                                         L_: out
    InterCopyContext _c{ linda_.U, L_, SourceState{ _K.value() }, {}, {}, {}, LookupMode::FromKeeper, {} };
    lua_pushnil(_K);                                                                               // _K: KeysDB nil                                     L_: out
    while (lua_next(_K, -2)) {                                                                     // _K: KeysDB key KeyUD                               L_: out
        KeyUD* const _key{ KeyUD::GetPtr(_K, kIdxTop) };
        _key->prepareAccess(_K, kIdxTop);                                                          // _K: KeysDB key fifo                                L_: out
        lua_pushvalue(_K, -2);                                                                     // _K: KeysDB key fifo key                            L_: out
        if (_c.interMove(1) != InterCopyResult::Success) {                                         // _K: KeysDB key fifo                                L_: out key
            raise_luaL_error(L_, "Internal error reading Keeper contents");
        }
        STACK_CHECK(L_, 2);
        lua_newtable(L_);                                                                          // _K: KeysDB key fifo                                L_: out key keyout
        if (_c.interMove(1) != InterCopyResult::Success) {                                         // _K: KeysDB key                                     L_: out key keyout fifo
            raise_luaL_error(L_, "Internal error reading Keeper contents");
        }
        // keyout.first
        lua_pushinteger(L_, _key->first);                                                          // _K: KeysDB key                                     L_: out key keyout fifo first
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "first");                                                             // _K: KeysDB key                                     L_: out key keyout fifo
        // keyout.count
        lua_pushinteger(L_, _key->count);                                                          // _K: KeysDB key                                     L_: out key keyout fifo count
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "count");                                                             // _K: KeysDB key                                     L_: out key keyout fifo
        // keyout.limit
        if (_key->limit >= 0) {
            lua_pushinteger(L_, _key->limit);                                                      // _K: KeysDB key                                     L_: out key keyout fifo limit
        } else {
            luaW_pushstring(L_, "unlimited");                                                      // _K: KeysDB key                                     L_: out key keyout fifo limit
        }
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "limit");                                                             // _K: KeysDB key                                     L_: out key keyout fifo
        // keyout.restrict
        switch (_key->restrict) {
        case LindaRestrict::None:
            luaW_pushstring(L_, "none");                                                           // _K: KeysDB key                                     L_: out key keyout fifo restrict
            break;
        case LindaRestrict::SetGet:
            luaW_pushstring(L_, "set/get");                                                        // _K: KeysDB key                                     L_: out key keyout fifo restrict
            break;
        case LindaRestrict::SendReceive:
            luaW_pushstring(L_, "send/receive");                                                   // _K: KeysDB key                                     L_: out key keyout fifo restrict
            break;
        }
        STACK_CHECK(L_, 5);
        lua_setfield(L_, -3, "restrict");                                                          // _K: KeysDB key                                     L_: out key keyout fifo
        // keyout.fifo
        lua_setfield(L_, -2, "fifo");                                                              // _K: KeysDB key                                     L_: out key keyout
        // out[key] = keyout
        lua_rawset(L_, -3);                                                                        // _K: KeysDB key                                     L_: out
        STACK_CHECK(L_, 1);
    }                                                                                              // _K: KeysDB                                         L_: out
    STACK_CHECK(L_, 1);
    lua_pop(_K, 1);                                                                                // _K:                                                L_: out
    STACK_CHECK(_K, 0);
    return 1;
}

// #################################################################################################
// #################################################################################################
// ########################################## Keepers ##############################################
// #################################################################################################
// #################################################################################################

void Keepers::DeleteKV::operator()(Keeper* const k_) const
{
    for (auto& _k : std::span<Keeper>(k_, count)) {
        _k.~Keeper();
    }
    U.internalAllocator.free(k_, count * sizeof(Keeper));
}

// #################################################################################################

void Keepers::collectGarbage()
{
    if (isClosing.test(std::memory_order_acquire)) {
        assert(false); // should never close more than once in practice
        return;
    }

    if (std::holds_alternative<std::monostate>(keeper_array)) {
        return;
    }

    auto _gcOneKeeper = [](Keeper& keeper_) {
        std::lock_guard<std::mutex> _guard(keeper_.mutex);
        if (keeper_.K) {
            lua_gc(keeper_.K, LUA_GCCOLLECT, 0);
        }
    };

    if (std::holds_alternative<Keeper>(keeper_array)) {
        _gcOneKeeper(std::get<Keeper>(keeper_array));
    } else {
        KV& _kv = std::get<KV>(keeper_array);

        // NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
        // when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
        // in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
        // which is early-outed with a keepers->nbKeepers null-check
        for (Keeper& _k : std::span<Keeper>{ _kv.keepers.get(), _kv.nbKeepers }) {
            _gcOneKeeper(_k);
        }
    }
}

// #################################################################################################

[[nodiscard]]
bool Keepers::close()
{
    if (isClosing.test_and_set(std::memory_order_release)) {
        return false; // should never close more than once in practice
    }

    // We may have not initialized the keepers if an error was raised in Universe::Create because of bad settings
    if (std::holds_alternative<std::monostate>(keeper_array)) {
        return true;
    }

    auto _closeOneKeeper = [](Keeper& keeper_) {
        lua_State* const _K{ std::exchange(keeper_.K, KeeperState{ static_cast<lua_State*>(nullptr) }) };
        if (_K) {
            lua_close(_K);
        }
        return _K ? true : false;
    };

    if (std::holds_alternative<Keeper>(keeper_array)) {
        _closeOneKeeper(std::get<Keeper>(keeper_array));
    } else {
        KV& _kv = std::get<KV>(keeper_array);

        // NOTE: imagine some keeper state N+1 currently holds a linda that uses another keeper N, and a _gc that will make use of it
        // when keeper N+1 is closed, object is GCed, linda operation is called, which attempts to acquire keeper N, whose Lua state no longer exists
        // in that case, the linda operation should do nothing. which means that these operations must check for keeper acquisition success
        // which is early-outed with a keepers->nbKeepers null-check
        for (Keeper& _k : std::span<Keeper>{ _kv.keepers.get(), std::exchange(_kv.nbKeepers, size_t{ 0 }) }) {
            if (!_closeOneKeeper(_k)) {
                // detected partial init: destroy only the mutexes that got initialized properly
                break;
            }
        }
    }

    keeper_array.emplace<std::monostate>();
    return true;
}

// #################################################################################################

[[nodiscard]]
Keeper* Keepers::getKeeper(KeeperIndex const idx_)
{
    if (isClosing.test(std::memory_order_acquire)) {
        return nullptr;
    }

    if (std::holds_alternative<std::monostate>(keeper_array)) {
        return nullptr;
    }

    if (std::holds_alternative<Keeper>(keeper_array)) {
        return &std::get<Keeper>(keeper_array);
    }

    return &std::get<KV>(keeper_array).keepers.get()[idx_];
}

// #################################################################################################

[[nodiscard]]
int Keepers::getNbKeepers() const
{
    if (isClosing.test(std::memory_order_acquire)) {
        return 0;
    }

    if (std::holds_alternative<std::monostate>(keeper_array)) {
        return 0;
    }

    if (std::holds_alternative<Keeper>(keeper_array)) {
        return 1;
    }

    return static_cast<int>(std::get<KV>(keeper_array).nbKeepers);
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

void Keepers::initialize(Universe& U_, lua_State* L_, size_t const nbKeepers_, int const gc_threshold_)
{
    gc_threshold = gc_threshold_;

    auto _initOneKeeper = [U = &U_, L = L_, gc_threshold = gc_threshold](Keeper& keeper_, int const i_) {
        STACK_CHECK_START_REL(L, 0);
        // note that we will leak K if we raise an error later
        KeeperState const _K{ state::CreateState(U, L, "keeper") };                                // L_: settings                                   _K:
        if (_K == nullptr) {
            raise_luaL_error(L, "out of memory while creating keeper states");
        }

        keeper_.K = _K;

        // Give a name to the state
        luaW_pushstring(_K, "Keeper #%d", i_ + 1);                                                 // L_: settings                                   _K: "Keeper #n"
        if constexpr (HAVE_DECODA_SUPPORT()) {
            lua_pushvalue(_K, -1);                                                                 //                                                _K: "Keeper #n" Keeper #n"
            lua_setglobal(_K, "decoda_name");                                                      // L_: settings                                   _K: "Keeper #n"
        }
        kLaneNameRegKey.setValue(_K, [](lua_State* L_) { lua_insert(L_, -2); });                   //                                                _K:

        STACK_CHECK_START_ABS(_K, 0);

        // copy the universe pointer in the keeper itself
        Universe::Store(_K, U);
        STACK_CHECK(_K, 0);

        // make sure 'package' is initialized in keeper states, so that we have require()
        // this because this is needed when transferring deep userdata object
        luaL_requiref(_K, LUA_LOADLIBNAME, luaopen_package, 1);                                    // L_: settings                                   _K: package
        lua_pop(_K, 1);                                                                            // L_: settings                                   _K:
        STACK_CHECK(_K, 0);
        tools::SerializeRequire(_K);
        STACK_CHECK(_K, 0);

        // copy package.path and package.cpath from the source state
        if (luaW_getmodule(L, LUA_LOADLIBNAME) != LuaType::NIL) {                                  // L_: settings package                           _K:
            // when copying with mode LookupMode::ToKeeper, error message is pushed at the top of the stack, not raised immediately
            InterCopyContext _c{ U, DestState{ _K.value() }, SourceState{ L }, {}, SourceIndex{ luaW_absindex(L, kIdxTop).value() }, {}, LookupMode::ToKeeper, {} };
            if (_c.interCopyPackage() != InterCopyResult::Success) {                               // L_: settings ... error_msg                     _K:
                // if something went wrong, the error message is at the top of the stack
                lua_remove(L, -2);                                                                 // L_: settings error_msg
                raise_lua_error(L);
            }
        }
        lua_pop(L, 1);                                                                             // L_: settings                                   _K:
        STACK_CHECK(L, 0);
        STACK_CHECK(_K, 0);

        // attempt to call on_state_create(), if we have one and it is a C function
        // (only support a C function because we can't transfer executable Lua code in keepers)
        // will raise an error in L_ in case of problem
        U->callOnStateCreate(_K, L, LookupMode::ToKeeper);

        // _R[kLindasRegKey] = {}
        kLindasRegKey.setValue(_K, [](lua_State* const L_) { lua_newtable(L_); });
        STACK_CHECK(_K, 0);

        // configure GC last
        if (gc_threshold >= 0) {
            lua_gc(_K, LUA_GCSTOP, 0);
        }
    };

    switch (nbKeepers_) {
    case 0:
        break;

    case 1:
        keeper_array.emplace<Keeper>();
        _initOneKeeper(std::get<Keeper>(keeper_array), 0);
        break;

    default:
        KV& _kv = keeper_array.emplace<KV>(
            std::unique_ptr<Keeper, DeleteKV>{ static_cast<Keeper*>(U_.internalAllocator.alloc(sizeof(Keeper) * nbKeepers_)), DeleteKV{ U_, nbKeepers_ } },
            nbKeepers_
        );
        // fak. std::ranges::views::enumerate is c++23 (would help having item and index iterated over simultaneously)
        int _i{};
        for (Keeper& _k : std::span<Keeper>{ _kv.keepers.get(), nbKeepers_ }) {
            new (&_k) Keeper{};
            _initOneKeeper(_k, _i++);
        }
    }
}

// #################################################################################################

LUAG_FUNC(collectgarbage)
{
    Universe* const _U{ Universe::Get(L_) };
    _U->keepers.collectGarbage();
    return 0;
}
