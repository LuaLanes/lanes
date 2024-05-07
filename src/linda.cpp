/*
 * LINDA.CPP                        Copyright (c) 2018-2024, Benoit Germain
 *
 * Linda deep userdata.
 */

/*
===============================================================================

Copyright (C) 2018-2024 benoit Germain <bnt.germain@gmail.com>

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

#include "linda.h"

#include "compat.h"
#include "keeper.h"
#include "lanes_private.h"
#include "lindafactory.h"
#include "threading.h"
#include "tools.h"
#include "universe.h"

#include <functional>

// #################################################################################################
// #################################################################################################

// Any hashing will do that maps pointers to [0..Universe::nb_keepers[ consistently.
// Pointers are often aligned by 8 or so - ignore the low order bits
// have to cast to unsigned long to avoid compilation warnings about loss of data when converting pointer-to-integer
static constexpr uintptr_t kPointerMagicShift{ 3 };

Linda::Linda(Universe* U_, LindaGroup group_, char const* name_, size_t len_)
: DeepPrelude{ LindaFactory::Instance }
, U{ U_ }
, keeperIndex{ (group_ ? group_ : static_cast<int>(std::bit_cast<uintptr_t>(this) >> kPointerMagicShift)) % U_->keepers->nb_keepers }
{
    setName(name_, len_);
}

// #################################################################################################

Linda::~Linda()
{
    if (std::holds_alternative<AllocatedName>(nameVariant)) {
        AllocatedName& name = std::get<AllocatedName>(nameVariant);
        U->internalAllocator.free(name.name, name.len);
    }
}

// #################################################################################################

void Linda::setName(char const* name_, size_t len_)
{
    // keep default
    if (!name_ || len_ == 0) {
        return;
    }
    ++len_; // don't forget terminating 0
    if (len_ < kEmbeddedNameLength) {
        nameVariant.emplace<EmbeddedName>();
        char* const name{ std::get<EmbeddedName>(nameVariant).data() };
        memcpy(name, name_, len_);
    } else {
        AllocatedName& name = std::get<AllocatedName>(nameVariant);
        name.name = static_cast<char*>(U->internalAllocator.alloc(len_));
        name.len = len_;
        memcpy(name.name, name_, len_);
    }
}

// #################################################################################################

char const* Linda::getName() const
{
    if (std::holds_alternative<AllocatedName>(nameVariant)) {
        AllocatedName const& name = std::get<AllocatedName>(nameVariant);
        return name.name;
    }
    if (std::holds_alternative<EmbeddedName>(nameVariant)) {
        char const* const name{ std::get<EmbeddedName>(nameVariant).data() };
        return name;
    }
    return nullptr;
}

// #################################################################################################

template <bool OPT>
[[nodiscard]] static inline Linda* ToLinda(lua_State* L_, int idx_)
{
    Linda* const linda{ static_cast<Linda*>(LindaFactory::Instance.toDeep(L_, idx_)) };
    if constexpr (!OPT) {
        luaL_argcheck(L_, linda != nullptr, idx_, "expecting a linda object"); // doesn't return if linda is nullptr
        LUA_ASSERT(L_, linda->U == universe_get(L_));
    }
    return linda;
}

// #################################################################################################

static void check_key_types(lua_State* L_, int start_, int end_)
{
    for (int i{ start_ }; i <= end_; ++i) {
        LuaType const t{ lua_type_as_enum(L_, i) };
        switch (t) {
        case LuaType::BOOLEAN:
        case LuaType::NUMBER:
        case LuaType::STRING:
            continue;

        case LuaType::LIGHTUSERDATA:
            static constexpr std::array<std::reference_wrapper<UniqueKey const>, 3> kKeysToCheck{ kLindaBatched, kCancelError, kNilSentinel };
            for (UniqueKey const& key : kKeysToCheck) {
                if (key.equals(L_, i)) {
                    raise_luaL_error(L_, "argument #%d: can't use %s as a key", i, key.debugName);
                    break;
                }
            }
            break;
        }
        raise_luaL_error(L_, "argument #%d: invalid key type (not a boolean, string, number or light userdata)", i);
    }
}

// #################################################################################################

// used to perform all linda operations that access keepers
int Linda::ProtectedCall(lua_State* L_, lua_CFunction f_)
{
    Linda* const linda{ ToLinda<false>(L_, 1) };

    // acquire the keeper
    Keeper* const K{ linda->acquireKeeper() };
    lua_State* const KL{ K ? K->L : nullptr };
    if (KL == nullptr)
        return 0;
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, lua_gettop(KL) == 0);

    // push the function to be called and move it before the arguments
    lua_pushcfunction(L_, f_);
    lua_insert(L_, 1);
    // do a protected call
    int const rc{ lua_pcall(L_, lua_gettop(L_) - 1, LUA_MULTRET, 0) };
    // whatever happens, the keeper state stack must be empty when we are done
    lua_settop(KL, 0);

    // release the keeper
    linda->releaseKeeper(K);

    // if there was an error, forward it
    if (rc != LUA_OK) {
        raise_lua_error(L_);
    }
    // return whatever the actual operation provided
    return lua_gettop(L_);
}

// #################################################################################################

/*
 * bool= linda:linda_send([timeout_secs=nil,] key_num|str|bool|lightuserdata, ...)
 *
 * Send one or more values to a Linda. If there is a limit, all values must fit.
 *
 * Returns:  'true' if the value was queued
 *           'false' for timeout (only happens when the queue size is limited)
 *           nil, kCancelError if cancelled
 */
LUAG_FUNC(linda_send)
{
    auto send = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        int key_i{ 2 }; // index of first key, if timeout not there

        std::chrono::time_point<std::chrono::steady_clock> until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
        if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
            lua_Duration const duration{ lua_tonumber(L_, 2) };
            if (duration.count() >= 0.0) {
                until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
            } else {
                raise_luaL_argerror(L_, 2, "duration cannot be < 0");
            }
            ++key_i;
        } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
            ++key_i;
        }

        // make sure the key is of a valid type
        check_key_types(L_, key_i, key_i);

        STACK_GROW(L_, 1);

        // make sure there is something to send
        if (lua_gettop(L_) == key_i) {
            raise_luaL_error(L_, "no data to send");
        }

        // convert nils to some special non-nil sentinel in sent values
        keeper_toggle_nil_sentinels(L_, key_i + 1, LookupMode::ToKeeper);
        bool ret{ false };
        CancelRequest cancel{ CancelRequest::None };
        KeeperCallResult pushed;
        {
            Lane* const lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
            Keeper* const K{ linda->whichKeeper() };
            KeeperState const KL{ K ? K->L : nullptr };
            if (KL == nullptr)
                return 0;

            STACK_CHECK_START_REL(KL, 0);
            for (bool try_again{ true };;) {
                if (lane != nullptr) {
                    cancel = lane->cancelRequest;
                }
                cancel = (cancel != CancelRequest::None) ? cancel : linda->cancelRequest;
                // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
                if (!try_again || cancel != CancelRequest::None) {
                    pushed.emplace(0);
                    break;
                }

                STACK_CHECK(KL, 0);
                pushed = keeper_call(linda->U, KL, KEEPER_API(send), L_, linda, key_i);
                if (!pushed.has_value()) {
                    break;
                }
                LUA_ASSERT(L_, pushed.value() == 1);

                ret = lua_toboolean(L_, -1) ? true : false;
                lua_pop(L_, 1);

                if (ret) {
                    // Wake up ALL waiting threads
                    linda->writeHappened.notify_all();
                    break;
                }

                // instant timout to bypass the wait syscall
                if (std::chrono::steady_clock::now() >= until) {
                    break; /* no wait; instant timeout */
                }

                // storage limit hit, wait until timeout or signalled that we should try again
                {
                    Lane::Status prev_status{ Lane::Error }; // prevent 'might be used uninitialized' warnings
                    if (lane != nullptr) {
                        // change status of lane to "waiting"
                        prev_status = lane->status; // Running, most likely
                        LUA_ASSERT(L_, prev_status == Lane::Running); // but check, just in case
                        lane->status = Lane::Waiting;
                        LUA_ASSERT(L_, lane->waiting_on == nullptr);
                        lane->waiting_on = &linda->readHappened;
                    }
                    // could not send because no room: wait until some data was read before trying again, or until timeout is reached
                    std::unique_lock<std::mutex> keeper_lock{ K->mutex, std::adopt_lock };
                    std::cv_status const status{ linda->readHappened.wait_until(keeper_lock, until) };
                    keeper_lock.release(); // we don't want to release the lock!
                    try_again = (status == std::cv_status::no_timeout); // detect spurious wakeups
                    if (lane != nullptr) {
                        lane->waiting_on = nullptr;
                        lane->status = prev_status;
                    }
                }
            }
            STACK_CHECK(KL, 0);
        }

        if (!pushed.has_value()) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }

        switch (cancel) {
        case CancelRequest::Soft:
            // if user wants to soft-cancel, the call returns lanes.cancel_error
            kCancelError.pushKey(L_);
            return 1;

        case CancelRequest::Hard:
            // raise an error interrupting execution only in case of hard cancel
            raise_cancel_error(L_); // raises an error and doesn't return

        default:
            lua_pushboolean(L_, ret); // true (success) or false (timeout)
            return 1;
        }
    };
    return Linda::ProtectedCall(L_, send);
}

// #################################################################################################

/*
 * 2 modes of operation
 * [val, key]= linda_receive( linda_ud, [timeout_secs_num=nil], key_num|str|bool|lightuserdata [, ...] )
 * Consumes a single value from the Linda, in any key.
 * Returns: received value (which is consumed from the slot), and the key which had it

 * [val1, ... valCOUNT]= linda_receive( linda_ud, [timeout_secs_num=-1], linda.batched, key_num|str|bool|lightuserdata, min_COUNT[, max_COUNT])
 * Consumes between min_COUNT and max_COUNT values from the linda, from a single key.
 * returns the actual consumed values, or nil if there weren't enough values to consume
 *
 */
LUAG_FUNC(linda_receive)
{
    auto receive = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        int key_i{ 2 }; // index of first key, if timeout not there

        std::chrono::time_point<std::chrono::steady_clock> until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
        if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
            lua_Duration const duration{ lua_tonumber(L_, 2) };
            if (duration.count() >= 0.0) {
                until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
            } else {
                raise_luaL_argerror(L_, 2, "duration cannot be < 0");
            }
            ++key_i;
        } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
            ++key_i;
        }

        keeper_api_t selected_keeper_receive{ nullptr };
        int expected_pushed_min{ 0 }, expected_pushed_max{ 0 };
        // are we in batched mode?
        kLindaBatched.pushKey(L_);
        int const is_batched{ lua501_equal(L_, key_i, -1) };
        lua_pop(L_, 1);
        if (is_batched) {
            // no need to pass linda.batched in the keeper state
            ++key_i;
            // make sure the keys are of a valid type
            check_key_types(L_, key_i, key_i);
            // receive multiple values from a single slot
            selected_keeper_receive = KEEPER_API(receive_batched);
            // we expect a user-defined amount of return value
            expected_pushed_min = (int) luaL_checkinteger(L_, key_i + 1);
            expected_pushed_max = (int) luaL_optinteger(L_, key_i + 2, expected_pushed_min);
            // don't forget to count the key in addition to the values
            ++expected_pushed_min;
            ++expected_pushed_max;
            if (expected_pushed_min > expected_pushed_max) {
                raise_luaL_error(L_, "batched min/max error");
            }
        } else {
            // make sure the keys are of a valid type
            check_key_types(L_, key_i, lua_gettop(L_));
            // receive a single value, checking multiple slots
            selected_keeper_receive = KEEPER_API(receive);
            // we expect a single (value, key) pair of returned values
            expected_pushed_min = expected_pushed_max = 2;
        }

        Lane* const lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
        Keeper* const K{ linda->whichKeeper() };
        KeeperState const KL{ K ? K->L : nullptr };
        if (KL == nullptr)
            return 0;

        CancelRequest cancel{ CancelRequest::None };
        KeeperCallResult pushed;
        STACK_CHECK_START_REL(KL, 0);
        for (bool try_again{ true };;) {
            if (lane != nullptr) {
                cancel = lane->cancelRequest;
            }
            cancel = (cancel != CancelRequest::None) ? cancel : linda->cancelRequest;
            // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
            if (!try_again || cancel != CancelRequest::None) {
                pushed.emplace(0);
                break;
            }

            // all arguments of receive() but the first are passed to the keeper's receive function
            pushed = keeper_call(linda->U, KL, selected_keeper_receive, L_, linda, key_i);
            if (!pushed.has_value()) {
                break;
            }
            if (pushed.value() > 0) {
                LUA_ASSERT(L_, pushed.value() >= expected_pushed_min && pushed.value() <= expected_pushed_max);
                // replace sentinels with real nils
                keeper_toggle_nil_sentinels(L_, lua_gettop(L_) - pushed.value(), LookupMode::FromKeeper);
                // To be done from within the 'K' locking area
                //
                linda->readHappened.notify_all();
                break;
            }

            if (std::chrono::steady_clock::now() >= until) {
                break; /* instant timeout */
            }

            // nothing received, wait until timeout or signalled that we should try again
            {
                Lane::Status prev_status{ Lane::Error }; // prevent 'might be used uninitialized' warnings
                if (lane != nullptr) {
                    // change status of lane to "waiting"
                    prev_status = lane->status; // Running, most likely
                    LUA_ASSERT(L_, prev_status == Lane::Running); // but check, just in case
                    lane->status = Lane::Waiting;
                    LUA_ASSERT(L_, lane->waiting_on == nullptr);
                    lane->waiting_on = &linda->writeHappened;
                }
                // not enough data to read: wakeup when data was sent, or when timeout is reached
                std::unique_lock<std::mutex> keeper_lock{ K->mutex, std::adopt_lock };
                std::cv_status const status{ linda->writeHappened.wait_until(keeper_lock, until) };
                keeper_lock.release();                              // we don't want to release the lock!
                try_again = (status == std::cv_status::no_timeout); // detect spurious wakeups
                if (lane != nullptr) {
                    lane->waiting_on = nullptr;
                    lane->status = prev_status;
                }
            }
        }
        STACK_CHECK(KL, 0);

        if (!pushed.has_value()) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }

        switch (cancel) {
        case CancelRequest::Soft:
            // if user wants to soft-cancel, the call returns kCancelError
            kCancelError.pushKey(L_);
            return 1;

        case CancelRequest::Hard:
            // raise an error interrupting execution only in case of hard cancel
            raise_cancel_error(L_); // raises an error and doesn't return

        default:
            return pushed.value();
        }
    };
    return Linda::ProtectedCall(L_, receive);
}

// #################################################################################################

/*
 * [true|lanes.cancel_error] = linda_set( linda_ud, key_num|str|bool|lightuserdata [, value [, ...]])
 *
 * Set one or more value to Linda.
 * TODO: what do we do if we set to non-nil and limit is 0?
 *
 * Existing slot value is replaced, and possible queued entries removed.
 */
LUAG_FUNC(linda_set)
{
    auto set = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        bool const has_value{ lua_gettop(L_) > 2 };
        // make sure the key is of a valid type (throws an error if not the case)
        check_key_types(L_, 2, 2);

        Keeper* const K{ linda->whichKeeper() };
        KeeperCallResult pushed;
        if (linda->cancelRequest == CancelRequest::None) {
            if (has_value) {
                // convert nils to some special non-nil sentinel in sent values
                keeper_toggle_nil_sentinels(L_, 3, LookupMode::ToKeeper);
            }
            pushed = keeper_call(linda->U, K->L, KEEPER_API(set), L_, linda, 2);
            if (pushed.has_value()) { // no error?
                LUA_ASSERT(L_, pushed.value() == 0 || pushed.value() == 1);

                if (has_value) {
                    // we put some data in the slot, tell readers that they should wake
                    linda->writeHappened.notify_all(); // To be done from within the 'K' locking area
                }
                if (pushed.value() == 1) {
                    // the key was full, but it is no longer the case, tell writers they should wake
                    LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TBOOLEAN && lua_toboolean(L_, -1) == 1);
                    linda->readHappened.notify_all(); // To be done from within the 'K' locking area
                }
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            pushed.emplace(1);
        }

        // must trigger any error after keeper state has been released
        return OptionalValue(pushed, L_, "tried to copy unsupported types");
    };
    return Linda::ProtectedCall(L_, set);
}

// #################################################################################################

/*
 * [val] = linda_count( linda_ud, [key [, ...]])
 *
 * Get a count of the pending elements in the specified keys
 */
LUAG_FUNC(linda_count)
{
    auto count = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        // make sure the keys are of a valid type
        check_key_types(L_, 2, lua_gettop(L_));

        Keeper* const K{ linda->whichKeeper() };
        KeeperCallResult const pushed{ keeper_call(linda->U, K->L, KEEPER_API(count), L_, linda, 2) };
        return OptionalValue(pushed, L_, "tried to count an invalid key");
    };
    return Linda::ProtectedCall(L_, count);
}

// #################################################################################################

/*
 * [val [, ...]] = linda_get( linda_ud, key_num|str|bool|lightuserdata [, count = 1])
 *
 * Get one or more values from Linda.
 */
LUAG_FUNC(linda_get)
{
    auto get = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        lua_Integer const count{ luaL_optinteger(L_, 3, 1) };
        luaL_argcheck(L_, count >= 1, 3, "count should be >= 1");
        luaL_argcheck(L_, lua_gettop(L_) <= 3, 4, "too many arguments");
        // make sure the key is of a valid type (throws an error if not the case)
        check_key_types(L_, 2, 2);

        KeeperCallResult pushed;
        if (linda->cancelRequest == CancelRequest::None) {
            Keeper* const K{ linda->whichKeeper() };
            pushed = keeper_call(linda->U, K->L, KEEPER_API(get), L_, linda, 2);
            if (pushed.value_or(0) > 0) {
                keeper_toggle_nil_sentinels(L_, lua_gettop(L_) - pushed.value(), LookupMode::FromKeeper);
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            pushed.emplace(1);
        }
        // an error can be raised if we attempt to read an unregistered function
        return OptionalValue(pushed, L_, "tried to copy unsupported types");
    };
    return Linda::ProtectedCall(L_, get);
}

// #################################################################################################

/*
 * [true] = linda_limit( linda_ud, key_num|str|bool|lightuserdata, int)
 *
 * Set limit to 1 Linda keys.
 * Optionally wake threads waiting to write on the linda, in case the limit enables them to do so
 */
LUAG_FUNC(linda_limit)
{
    auto limit = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        // make sure we got 3 arguments: the linda, a key and a limit
        luaL_argcheck(L_, lua_gettop(L_) == 3, 2, "wrong number of arguments");
        // make sure we got a numeric limit
        luaL_checknumber(L_, 3);
        // make sure the key is of a valid type
        check_key_types(L_, 2, 2);

        KeeperCallResult pushed;
        if (linda->cancelRequest == CancelRequest::None) {
            Keeper* const K{ linda->whichKeeper() };
            pushed = keeper_call(linda->U, K->L, KEEPER_API(limit), L_, linda, 2);
            LUA_ASSERT(L_, pushed.has_value() && (pushed.value() == 0 || pushed.value() == 1)); // no error, optional boolean value saying if we should wake blocked writer threads
            if (pushed.value() == 1) {
                LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TBOOLEAN && lua_toboolean(L_, -1) == 1);
                linda->readHappened.notify_all(); // To be done from within the 'K' locking area
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            pushed.emplace(1);
        }
        // propagate pushed boolean if any
        return pushed.value();
    };
    return Linda::ProtectedCall(L_, limit);
}

// #################################################################################################

/*
 * (void) = linda_cancel( linda_ud, "read"|"write"|"both"|"none")
 *
 * Signal linda so that waiting threads wake up as if their own lane was cancelled
 */
LUAG_FUNC(linda_cancel)
{
    Linda* const linda{ ToLinda<false>(L_, 1) };
    char const* who = luaL_optstring(L_, 2, "both");
    // make sure we got 3 arguments: the linda, a key and a limit
    luaL_argcheck(L_, lua_gettop(L_) <= 2, 2, "wrong number of arguments");

    linda->cancelRequest = CancelRequest::Soft;
    if (strcmp(who, "both") == 0) { // tell everyone writers to wake up
        linda->writeHappened.notify_all();
        linda->readHappened.notify_all();
    } else if (strcmp(who, "none") == 0) { // reset flag
        linda->cancelRequest = CancelRequest::None;
    } else if (strcmp(who, "read") == 0) { // tell blocked readers to wake up
        linda->writeHappened.notify_all();
    } else if (strcmp(who, "write") == 0) { // tell blocked writers to wake up
        linda->readHappened.notify_all();
    } else {
        raise_luaL_error(L_, "unknown wake hint '%s'", who);
    }
    return 0;
}

// #################################################################################################

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
LUAG_FUNC(linda_deep)
{
    Linda* const linda{ ToLinda<false>(L_, 1) };
    lua_pushlightuserdata(L_, linda); // just the address
    return 1;
}

// #################################################################################################

/*
 * string = linda:__tostring( linda_ud)
 *
 * Return the stringification of a linda
 *
 * Useful for concatenation or debugging purposes
 */

template <bool OPT>
[[nodiscard]] static int LindaToString(lua_State* L_, int idx_)
{
    Linda* const linda{ ToLinda<OPT>(L_, idx_) };
    if (linda != nullptr) {
        char text[128];
        int len;
        if (linda->getName())
            len = sprintf(text, "Linda: %.*s", (int) sizeof(text) - 8, linda->getName());
        else
            len = sprintf(text, "Linda: %p", linda);
        lua_pushlstring(L_, text, len);
        return 1;
    }
    return 0;
}

// #################################################################################################

LUAG_FUNC(linda_tostring)
{
    return LindaToString<false>(L_, 1);
}

// #################################################################################################

/*
 * string = linda:__concat( a, b)
 *
 * Return the concatenation of a pair of items, one of them being a linda
 *
 * Useful for concatenation or debugging purposes
 */
LUAG_FUNC(linda_concat)
{                                                                                                  // L_: linda1? linda2?
    bool atLeastOneLinda{ false };
    // Lua semantics enforce that one of the 2 arguments is a Linda, but not necessarily both.
    if (LindaToString<true>(L_, 1)) {
        atLeastOneLinda = true;
        lua_replace(L_, 1);
    }
    if (LindaToString<true>(L_, 2)) {
        atLeastOneLinda = true;
        lua_replace(L_, 2);
    }
    if (!atLeastOneLinda) { // should not be possible
        raise_luaL_error(L_, "internal error: linda_concat called on non-Linda");
    }
    lua_concat(L_, 2);
    return 1;
}

// #################################################################################################

/*
 * table = linda:dump()
 * return a table listing all pending data inside the linda
 */
LUAG_FUNC(linda_dump)
{
    auto dump = [](lua_State* L_) {
        Linda* const linda{ ToLinda<false>(L_, 1) };
        return keeper_push_linda_storage(*linda, DestState{ L_ });
    };
    return Linda::ProtectedCall(L_, dump);
}

// #################################################################################################

/*
 * table/string = linda:__towatch()
 * return a table listing all pending data inside the linda, or the stringified linda if empty
 */
LUAG_FUNC(linda_towatch)
{
    Linda* const linda{ ToLinda<false>(L_, 1) };
    int pushed{ keeper_push_linda_storage(*linda, DestState{ L_ }) };
    if (pushed == 0) {
        // if the linda is empty, don't return nil
        pushed = LindaToString<false>(L_, 1);
    }
    return pushed;
}

// #################################################################################################

namespace global {
    static luaL_Reg const sLindaMT[] = {
        { "__concat", LG_linda_concat },
        { "__tostring", LG_linda_tostring },
        { "__towatch", LG_linda_towatch }, // Decoda __towatch support
        { "cancel", LG_linda_cancel },
        { "count", LG_linda_count },
        { "deep", LG_linda_deep },
        { "dump", LG_linda_dump },
        { "get", LG_linda_get },
        { "limit", LG_linda_limit },
        { "receive", LG_linda_receive },
        { "send", LG_linda_send },
        { "set", LG_linda_set },
        { nullptr, nullptr }
    };
} // namespace global
// it's somewhat awkward to instanciate the LindaFactory here instead of lindafactory.cpp,
// but that's necessary to provide s_LindaMT without exposing it outside linda.cpp.
/*static*/ LindaFactory LindaFactory::Instance{ global::sLindaMT };

// #################################################################################################

/*
 * ud = lanes.linda( [name[,group]])
 *
 * returns a linda object, or raises an error if creation failed
 */
LUAG_FUNC(linda)
{
    int const top{ lua_gettop(L_) };
    luaL_argcheck(L_, top <= 2, top, "too many arguments");
    if (top == 1) {
        LuaType const t{ lua_type_as_enum(L_, 1) };
        luaL_argcheck(L_, t == LuaType::STRING || t == LuaType::NUMBER, 1, "wrong parameter (should be a string or a number)");
    } else if (top == 2) {
        luaL_checktype(L_, 1, LUA_TSTRING);
        luaL_checktype(L_, 2, LUA_TNUMBER);
    }
    return LindaFactory::Instance.pushDeepUserdata(DestState{ L_ }, 0);
}
