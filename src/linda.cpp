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

#include "lane.h"
#include "lindafactory.h"
#include "tools.h"

#include <functional>

// #################################################################################################

static void check_key_types(lua_State* L_, int start_, int end_)
{
    for (int _i{ start_ }; _i <= end_; ++_i) {
        LuaType const t{ lua_type_as_enum(L_, _i) };
        switch (t) {
        case LuaType::BOOLEAN:
        case LuaType::NUMBER:
        case LuaType::STRING:
            continue;

        case LuaType::LIGHTUSERDATA:
            static constexpr std::array<std::reference_wrapper<UniqueKey const>, 3> kKeysToCheck{ kLindaBatched, kCancelError, kNilSentinel };
            for (UniqueKey const& key : kKeysToCheck) {
                if (key.equals(L_, _i)) {
                    raise_luaL_error(L_, "argument #%d: can't use %s as a key", _i, key.debugName);
                    break;
                }
            }
            break;
        }
        raise_luaL_error(L_, "argument #%d: invalid key type (not a boolean, string, number or light userdata)", _i);
    }
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
    Linda* const _linda{ ToLinda<OPT>(L_, idx_) };
    if (_linda != nullptr) {
        char _text[128];
        int _len;
        if (_linda->getName())
            _len = sprintf(_text, "Linda: %.*s", (int) sizeof(_text) - 8, _linda->getName());
        else
            _len = sprintf(_text, "Linda: %p", _linda);
        lua_pushlstring(L_, _text, _len);
        return 1;
    }
    return 0;
}

// #################################################################################################

template <bool OPT>
[[nodiscard]] static inline Linda* ToLinda(lua_State* L_, int idx_)
{
    Linda* const _linda{ static_cast<Linda*>(LindaFactory::Instance.toDeep(L_, idx_)) };
    if constexpr (!OPT) {
        luaL_argcheck(L_, _linda != nullptr, idx_, "expecting a linda object"); // doesn't return if linda is nullptr
        LUA_ASSERT(L_, _linda->U == universe_get(L_));
    }
    return _linda;
}

// #################################################################################################
// #################################### Linda implementation #######################################
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
        AllocatedName& _name = std::get<AllocatedName>(nameVariant);
        U->internalAllocator.free(_name.name, _name.len);
    }
}

// #################################################################################################

char const* Linda::getName() const
{
    if (std::holds_alternative<AllocatedName>(nameVariant)) {
        AllocatedName const& _name = std::get<AllocatedName>(nameVariant);
        return _name.name;
    }
    if (std::holds_alternative<EmbeddedName>(nameVariant)) {
        char const* const _name{ std::get<EmbeddedName>(nameVariant).data() };
        return _name;
    }
    return nullptr;
}

// #################################################################################################

// used to perform all linda operations that access keepers
int Linda::ProtectedCall(lua_State* L_, lua_CFunction f_)
{
    Linda* const _linda{ ToLinda<false>(L_, 1) };

    // acquire the keeper
    Keeper* const _K{ _linda->acquireKeeper() };
    lua_State* const _KL{ _K ? _K->L : nullptr };
    if (_KL == nullptr)
        return 0;
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, lua_gettop(_KL) == 0);

    // push the function to be called and move it before the arguments
    lua_pushcfunction(L_, f_);
    lua_insert(L_, 1);
    // do a protected call
    int const _rc{ lua_pcall(L_, lua_gettop(L_) - 1, LUA_MULTRET, 0) };
    // whatever happens, the keeper state stack must be empty when we are done
    lua_settop(_KL, 0);

    // release the keeper
    _linda->releaseKeeper(_K);

    // if there was an error, forward it
    if (_rc != LUA_OK) {
        raise_lua_error(L_);
    }
    // return whatever the actual operation provided
    return lua_gettop(L_);
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
        char* const _name{ std::get<EmbeddedName>(nameVariant).data() };
        memcpy(_name, name_, len_);
    } else {
        AllocatedName& _name = std::get<AllocatedName>(nameVariant);
        _name.name = static_cast<char*>(U->internalAllocator.alloc(len_));
        _name.len = len_;
        memcpy(_name.name, name_, len_);
    }
}

// #################################################################################################
// ########################################## Lua API ##############################################
// #################################################################################################

/*
 * (void) = linda_cancel( linda_ud, "read"|"write"|"both"|"none")
 *
 * Signal linda so that waiting threads wake up as if their own lane was cancelled
 */
LUAG_FUNC(linda_cancel)
{
    Linda* const _linda{ ToLinda<false>(L_, 1) };
    char const* _who{ luaL_optstring(L_, 2, "both") };
    // make sure we got 3 arguments: the linda, a key and a limit
    luaL_argcheck(L_, lua_gettop(L_) <= 2, 2, "wrong number of arguments");

    _linda->cancelRequest = CancelRequest::Soft;
    if (strcmp(_who, "both") == 0) { // tell everyone writers to wake up
        _linda->writeHappened.notify_all();
        _linda->readHappened.notify_all();
    } else if (strcmp(_who, "none") == 0) { // reset flag
        _linda->cancelRequest = CancelRequest::None;
    } else if (strcmp(_who, "read") == 0) { // tell blocked readers to wake up
        _linda->writeHappened.notify_all();
    } else if (strcmp(_who, "write") == 0) { // tell blocked writers to wake up
        _linda->readHappened.notify_all();
    } else {
        raise_luaL_error(L_, "unknown wake hint '%s'", _who);
    }
    return 0;
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
    bool _atLeastOneLinda{ false };
    // Lua semantics enforce that one of the 2 arguments is a Linda, but not necessarily both.
    if (LindaToString<true>(L_, 1)) {
        _atLeastOneLinda = true;
        lua_replace(L_, 1);
    }
    if (LindaToString<true>(L_, 2)) {
        _atLeastOneLinda = true;
        lua_replace(L_, 2);
    }
    if (!_atLeastOneLinda) { // should not be possible
        raise_luaL_error(L_, "internal error: linda_concat called on non-Linda");
    }
    lua_concat(L_, 2);
    return 1;
}

// #################################################################################################

/*
 * [val] = linda_count( linda_ud, [key [, ...]])
 *
 * Get a count of the pending elements in the specified keys
 */
LUAG_FUNC(linda_count)
{
    auto _count = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        // make sure the keys are of a valid type
        check_key_types(L_, 2, lua_gettop(L_));

        Keeper* const _K{ _linda->whichKeeper() };
        KeeperCallResult const _pushed{ keeper_call(_K->L, KEEPER_API(count), L_, _linda, 2) };
        return OptionalValue(_pushed, L_, "tried to count an invalid key");
    };
    return Linda::ProtectedCall(L_, _count);
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
    Linda* const _linda{ ToLinda<false>(L_, 1) };
    lua_pushlightuserdata(L_, _linda); // just the address
    return 1;
}

// #################################################################################################

/*
 * table = linda:dump()
 * return a table listing all pending data inside the linda
 */
LUAG_FUNC(linda_dump)
{
    auto _dump = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        return keeper_push_linda_storage(*_linda, DestState{ L_ });
    };
    return Linda::ProtectedCall(L_, _dump);
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
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        lua_Integer const _count{ luaL_optinteger(L_, 3, 1) };
        luaL_argcheck(L_, _count >= 1, 3, "count should be >= 1");
        luaL_argcheck(L_, lua_gettop(L_) <= 3, 4, "too many arguments");
        // make sure the key is of a valid type (throws an error if not the case)
        check_key_types(L_, 2, 2);

        KeeperCallResult _pushed;
        if (_linda->cancelRequest == CancelRequest::None) {
            Keeper* const _K{ _linda->whichKeeper() };
            _pushed = keeper_call(_K->L, KEEPER_API(get), L_, _linda, 2);
            if (_pushed.value_or(0) > 0) {
                keeper_toggle_nil_sentinels(L_, lua_gettop(L_) - _pushed.value(), LookupMode::FromKeeper);
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            _pushed.emplace(1);
        }
        // an error can be raised if we attempt to read an unregistered function
        return OptionalValue(_pushed, L_, "tried to copy unsupported types");
    };
    return Linda::ProtectedCall(L_, get);
}

// #################################################################################################

/*
 * [true] = linda_limit( linda_ud, key_num|str|bool|lightuserdata, int)
 *
 * Set limit to 1 Linda keys.
 * Optionally wake threads waiting to write on the linda, in case the limit enables them to do so
 * Limit can be 0 to completely block everything
 */
LUAG_FUNC(linda_limit)
{
    auto _limit = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        // make sure we got 3 arguments: the linda, a key and a limit
        luaL_argcheck(L_, lua_gettop(L_) == 3, 2, "wrong number of arguments");
        // make sure we got a numeric limit
        lua_Number const _limit{ luaL_checknumber(L_, 3) };
        if (_limit < 1) {
            raise_luaL_argerror(L_, 3, "limit must be >= 0");
        }
        // make sure the key is of a valid type
        check_key_types(L_, 2, 2);

        KeeperCallResult _pushed;
        if (_linda->cancelRequest == CancelRequest::None) {
            Keeper* const _K{ _linda->whichKeeper() };
            _pushed = keeper_call(_K->L, KEEPER_API(limit), L_, _linda, 2);
            LUA_ASSERT(L_, _pushed.has_value() && (_pushed.value() == 0 || _pushed.value() == 1)); // no error, optional boolean value saying if we should wake blocked writer threads
            if (_pushed.value() == 1) {
                LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TBOOLEAN && lua_toboolean(L_, -1) == 1);
                _linda->readHappened.notify_all(); // To be done from within the 'K' locking area
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            _pushed.emplace(1);
        }
        // propagate pushed boolean if any
        return _pushed.value();
    };
    return Linda::ProtectedCall(L_, _limit);
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
    auto _receive = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        int _key_i{ 2 }; // index of first key, if timeout not there

        std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
        if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
            lua_Duration const _duration{ lua_tonumber(L_, 2) };
            if (_duration.count() >= 0.0) {
                _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(_duration);
            } else {
                raise_luaL_argerror(L_, 2, "duration cannot be < 0");
            }
            ++_key_i;
        } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
            ++_key_i;
        }

        keeper_api_t _selected_keeper_receive{ nullptr };
        int _expected_pushed_min{ 0 }, _expected_pushed_max{ 0 };
        // are we in batched mode?
        kLindaBatched.pushKey(L_);
        int const _is_batched{ lua501_equal(L_, _key_i, -1) };
        lua_pop(L_, 1);
        if (_is_batched) {
            // no need to pass linda.batched in the keeper state
            ++_key_i;
            // make sure the keys are of a valid type
            check_key_types(L_, _key_i, _key_i);
            // receive multiple values from a single slot
            _selected_keeper_receive = KEEPER_API(receive_batched);
            // we expect a user-defined amount of return value
            _expected_pushed_min = (int) luaL_checkinteger(L_, _key_i + 1);
            _expected_pushed_max = (int) luaL_optinteger(L_, _key_i + 2, _expected_pushed_min);
            // don't forget to count the key in addition to the values
            ++_expected_pushed_min;
            ++_expected_pushed_max;
            if (_expected_pushed_min > _expected_pushed_max) {
                raise_luaL_error(L_, "batched min/max error");
            }
        } else {
            // make sure the keys are of a valid type
            check_key_types(L_, _key_i, lua_gettop(L_));
            // receive a single value, checking multiple slots
            _selected_keeper_receive = KEEPER_API(receive);
            // we expect a single (value, key) pair of returned values
            _expected_pushed_min = _expected_pushed_max = 2;
        }

        Lane* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
        Keeper* const _K{ _linda->whichKeeper() };
        KeeperState const _KL{ _K ? _K->L : nullptr };
        if (_KL == nullptr)
            return 0;

        CancelRequest _cancel{ CancelRequest::None };
        KeeperCallResult _pushed;
        STACK_CHECK_START_REL(_KL, 0);
        for (bool _try_again{ true };;) {
            if (_lane != nullptr) {
                _cancel = _lane->cancelRequest;
            }
            _cancel = (_cancel != CancelRequest::None) ? _cancel : _linda->cancelRequest;
            // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
            if (!_try_again || _cancel != CancelRequest::None) {
                _pushed.emplace(0);
                break;
            }

            // all arguments of receive() but the first are passed to the keeper's receive function
            _pushed = keeper_call(_KL, _selected_keeper_receive, L_, _linda, _key_i);
            if (!_pushed.has_value()) {
                break;
            }
            if (_pushed.value() > 0) {
                LUA_ASSERT(L_, _pushed.value() >= _expected_pushed_min && _pushed.value() <= _expected_pushed_max);
                // replace sentinels with real nils
                keeper_toggle_nil_sentinels(L_, lua_gettop(L_) - _pushed.value(), LookupMode::FromKeeper);
                // To be done from within the 'K' locking area
                //
                _linda->readHappened.notify_all();
                break;
            }

            if (std::chrono::steady_clock::now() >= _until) {
                break; /* instant timeout */
            }

            // nothing received, wait until timeout or signalled that we should try again
            {
                Lane::Status _prev_status{ Lane::Error }; // prevent 'might be used uninitialized' warnings
                if (_lane != nullptr) {
                    // change status of lane to "waiting"
                    _prev_status = _lane->status; // Running, most likely
                    LUA_ASSERT(L_, _prev_status == Lane::Running); // but check, just in case
                    _lane->status = Lane::Waiting;
                    LUA_ASSERT(L_, _lane->waiting_on == nullptr);
                    _lane->waiting_on = &_linda->writeHappened;
                }
                // not enough data to read: wakeup when data was sent, or when timeout is reached
                std::unique_lock<std::mutex> _keeper_lock{ _K->mutex, std::adopt_lock };
                std::cv_status const _status{ _linda->writeHappened.wait_until(_keeper_lock, _until) };
                _keeper_lock.release();                              // we don't want to release the lock!
                _try_again = (_status == std::cv_status::no_timeout); // detect spurious wakeups
                if (_lane != nullptr) {
                    _lane->waiting_on = nullptr;
                    _lane->status = _prev_status;
                }
            }
        }
        STACK_CHECK(_KL, 0);

        if (!_pushed.has_value()) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }

        switch (_cancel) {
        case CancelRequest::Soft:
            // if user wants to soft-cancel, the call returns kCancelError
            kCancelError.pushKey(L_);
            return 1;

        case CancelRequest::Hard:
            // raise an error interrupting execution only in case of hard cancel
            raise_cancel_error(L_); // raises an error and doesn't return

        default:
            return _pushed.value();
        }
    };
    return Linda::ProtectedCall(L_, _receive);
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
    auto _send = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        int _key_i{ 2 }; // index of first key, if timeout not there

        std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
        if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
            lua_Duration const _duration{ lua_tonumber(L_, 2) };
            if (_duration.count() >= 0.0) {
                _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(_duration);
            } else {
                raise_luaL_argerror(L_, 2, "duration cannot be < 0");
            }
            ++_key_i;
        } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
            ++_key_i;
        }

        // make sure the key is of a valid type
        check_key_types(L_, _key_i, _key_i);

        STACK_GROW(L_, 1);

        // make sure there is something to send
        if (lua_gettop(L_) == _key_i) {
            raise_luaL_error(L_, "no data to send");
        }

        // convert nils to some special non-nil sentinel in sent values
        keeper_toggle_nil_sentinels(L_, _key_i + 1, LookupMode::ToKeeper);
        bool _ret{ false };
        CancelRequest _cancel{ CancelRequest::None };
        KeeperCallResult _pushed;
        {
            Lane* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
            Keeper* const _K{ _linda->whichKeeper() };
            KeeperState const _KL{ _K ? _K->L : nullptr };
            if (_KL == nullptr)
                return 0;

            STACK_CHECK_START_REL(_KL, 0);
            for (bool _try_again{ true };;) {
                if (_lane != nullptr) {
                    _cancel = _lane->cancelRequest;
                }
                _cancel = (_cancel != CancelRequest::None) ? _cancel : _linda->cancelRequest;
                // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
                if (!_try_again || _cancel != CancelRequest::None) {
                    _pushed.emplace(0);
                    break;
                }

                STACK_CHECK(_KL, 0);
                _pushed = keeper_call(_KL, KEEPER_API(send), L_, _linda, _key_i);
                if (!_pushed.has_value()) {
                    break;
                }
                LUA_ASSERT(L_, _pushed.value() == 1);

                _ret = lua_toboolean(L_, -1) ? true : false;
                lua_pop(L_, 1);

                if (_ret) {
                    // Wake up ALL waiting threads
                    _linda->writeHappened.notify_all();
                    break;
                }

                // instant timout to bypass the wait syscall
                if (std::chrono::steady_clock::now() >= _until) {
                    break; /* no wait; instant timeout */
                }

                // storage limit hit, wait until timeout or signalled that we should try again
                {
                    Lane::Status _prev_status{ Lane::Error }; // prevent 'might be used uninitialized' warnings
                    if (_lane != nullptr) {
                        // change status of lane to "waiting"
                        _prev_status = _lane->status; // Running, most likely
                        LUA_ASSERT(L_, _prev_status == Lane::Running); // but check, just in case
                        _lane->status = Lane::Waiting;
                        LUA_ASSERT(L_, _lane->waiting_on == nullptr);
                        _lane->waiting_on = &_linda->readHappened;
                    }
                    // could not send because no room: wait until some data was read before trying again, or until timeout is reached
                    std::unique_lock<std::mutex> _keeper_lock{ _K->mutex, std::adopt_lock };
                    std::cv_status const status{ _linda->readHappened.wait_until(_keeper_lock, _until) };
                    _keeper_lock.release(); // we don't want to release the lock!
                    _try_again = (status == std::cv_status::no_timeout); // detect spurious wakeups
                    if (_lane != nullptr) {
                        _lane->waiting_on = nullptr;
                        _lane->status = _prev_status;
                    }
                }
            }
            STACK_CHECK(_KL, 0);
        }

        if (!_pushed.has_value()) {
            raise_luaL_error(L_, "tried to copy unsupported types");
        }

        switch (_cancel) {
        case CancelRequest::Soft:
            // if user wants to soft-cancel, the call returns lanes.cancel_error
            kCancelError.pushKey(L_);
            return 1;

        case CancelRequest::Hard:
            // raise an error interrupting execution only in case of hard cancel
            raise_cancel_error(L_); // raises an error and doesn't return

        default:
            lua_pushboolean(L_, _ret); // true (success) or false (timeout)
            return 1;
        }
    };
    return Linda::ProtectedCall(L_, _send);
}

// #################################################################################################

/*
 * [true|lanes.cancel_error] = linda_set( linda_ud, key_num|str|bool|lightuserdata [, value [, ...]])
 *
 * Set one or more value to Linda. Ignores limits.
 *
 * Existing slot value is replaced, and possible queued entries removed.
 */
LUAG_FUNC(linda_set)
{
    auto set = [](lua_State* L_) {
        Linda* const _linda{ ToLinda<false>(L_, 1) };
        bool const _has_value{ lua_gettop(L_) > 2 };
        // make sure the key is of a valid type (throws an error if not the case)
        check_key_types(L_, 2, 2);

        Keeper* const _K{ _linda->whichKeeper() };
        KeeperCallResult _pushed;
        if (_linda->cancelRequest == CancelRequest::None) {
            if (_has_value) {
                // convert nils to some special non-nil sentinel in sent values
                keeper_toggle_nil_sentinels(L_, 3, LookupMode::ToKeeper);
            }
            _pushed = keeper_call(_K->L, KEEPER_API(set), L_, _linda, 2);
            if (_pushed.has_value()) { // no error?
                LUA_ASSERT(L_, _pushed.value() == 0 || _pushed.value() == 1);

                if (_has_value) {
                    // we put some data in the slot, tell readers that they should wake
                    _linda->writeHappened.notify_all(); // To be done from within the 'K' locking area
                }
                if (_pushed.value() == 1) {
                    // the key was full, but it is no longer the case, tell writers they should wake
                    LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TBOOLEAN && lua_toboolean(L_, -1) == 1);
                    _linda->readHappened.notify_all(); // To be done from within the 'K' locking area
                }
            }
        } else { // linda is cancelled
            // do nothing and return lanes.cancel_error
            kCancelError.pushKey(L_);
            _pushed.emplace(1);
        }

        // must trigger any error after keeper state has been released
        return OptionalValue(_pushed, L_, "tried to copy unsupported types");
    };
    return Linda::ProtectedCall(L_, set);
}

// #################################################################################################

LUAG_FUNC(linda_tostring)
{
    return LindaToString<false>(L_, 1);
}

// #################################################################################################

/*
 * table/string = linda:__towatch()
 * return a table listing all pending data inside the linda, or the stringified linda if empty
 */
LUAG_FUNC(linda_towatch)
{
    Linda* const _linda{ ToLinda<false>(L_, 1) };
    int _pushed{ keeper_push_linda_storage(*_linda, DestState{ L_ }) };
    if (_pushed == 0) {
        // if the linda is empty, don't return nil
        _pushed = LindaToString<false>(L_, 1);
    }
    return _pushed;
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
// #################################################################################################

/*
 * ud = lanes.linda( [name[,group]])
 *
 * returns a linda object, or raises an error if creation failed
 */
LUAG_FUNC(linda)
{
    int const _top{ lua_gettop(L_) };
    luaL_argcheck(L_, _top <= 2, _top, "too many arguments");
    if (_top == 1) {
        LuaType const _t{ lua_type_as_enum(L_, 1) };
        luaL_argcheck(L_, _t == LuaType::STRING || _t == LuaType::NUMBER, 1, "wrong parameter (should be a string or a number)");
    } else if (_top == 2) {
        luaL_checktype(L_, 1, LUA_TSTRING);
        luaL_checktype(L_, 2, LUA_TNUMBER);
    }
    return LindaFactory::Instance.pushDeepUserdata(DestState{ L_ }, 0);
}
