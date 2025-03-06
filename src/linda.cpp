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

#include "_pch.hpp"
#include "linda.hpp"

#include "lane.hpp"
#include "lindafactory.hpp"
#include "tools.hpp"

// #################################################################################################
// #################################################################################################
namespace {
    // #############################################################################################
    // #############################################################################################


    static void CheckKeyTypes(lua_State* const L_, StackIndex const start_, StackIndex const end_)
    {
        STACK_CHECK_START_REL(L_, 0);
        for (StackIndex const _i : std::ranges::iota_view{ start_, StackIndex{ end_ + 1 } }) {
            LuaType const _t{ luaG_type(L_, _i) };
            switch (_t) {
            case LuaType::BOOLEAN:
            case LuaType::NUMBER:
            case LuaType::STRING:
                break;

            case LuaType::USERDATA:
                if (!DeepFactory::IsDeepUserdata(L_, _i)) {
                    raise_luaL_error(L_, "argument #%d: can't use non-deep userdata as a slot", _i);
                }
                break;

            case LuaType::LIGHTUSERDATA:
                {
                    static constexpr std::array<std::reference_wrapper<UniqueKey const>, 3> kKeysToCheck{ kLindaBatched, kCancelError, kNilSentinel };
                    for (UniqueKey const& _key : kKeysToCheck) {
                        if (_key.equals(L_, _i)) {
                            raise_luaL_error(L_, "argument #%d: can't use %s as a slot", _i, _key.debugName.data());
                            break;
                        }
                    }
                }
                break;

            default:
                raise_luaL_error(L_, "argument #%d: invalid slot type (not a boolean, string, number or light userdata)", _i);
            }
        }
        STACK_CHECK(L_, 0);
    }

    // #############################################################################################

    template <bool OPT>
    [[nodiscard]]
    static inline Linda* ToLinda(lua_State* const L_, StackIndex const idx_)
    {
        Linda* const _linda{ static_cast<Linda*>(LindaFactory::Instance.toDeep(L_, idx_)) };
        if constexpr (!OPT) {
            luaL_argcheck(L_, _linda != nullptr, idx_, "expecting a linda object"); // doesn't return if linda is nullptr
            LUA_ASSERT(L_, _linda->U == Universe::Get(L_));
        }
        return _linda;
    }

    // #############################################################################################

    /*
     * string = linda:__tostring( linda_ud)
     *
     * Return the stringification of a linda
     *
     * Useful for concatenation or debugging purposes
     */

    template <bool OPT>
    [[nodiscard]]
    static int LindaToString(lua_State* const L_, StackIndex const idx_)
    {
        Linda* const _linda{ ToLinda<OPT>(L_, idx_) };
        if (_linda != nullptr) {
            luaG_pushstring(L_, "Linda: ");
            std::string_view const _lindaName{ _linda->getName() };
            if (!_lindaName.empty()) {
                luaG_pushstring(L_, _lindaName);
            } else {
                // obfuscate the pointer so that we can't read the value with our eyes out of a script
                luaG_pushstring(L_, "%p", _linda->obfuscated());
            }
            lua_concat(L_, 2);
            return 1;
        }
        return 0;
    }

    // #############################################################################################
    // #############################################################################################
} // namespace
// #################################################################################################
// #################################################################################################

LUAG_FUNC(linda);

// #################################################################################################
// #################################################################################################
// #################################### Linda implementation #######################################
// #################################################################################################
// #################################################################################################

Linda::Linda(Universe* const U_, LindaGroup const group_, std::string_view const& name_)
: DeepPrelude{ LindaFactory::Instance }
, U{ U_ }
, keeperIndex{ group_ % U_->keepers.getNbKeepers() }
{
    setName(name_);
}

// #################################################################################################

Linda::~Linda()
{
    freeAllocatedName();
}

// #################################################################################################

Keeper* Linda::acquireKeeper() const
{
    // can be nullptr if this happens during main state shutdown (lanes is being GC'ed -> no keepers)
    Keeper* const _keeper{ whichKeeper() };
    if (_keeper) {
        _keeper->mutex.lock();
    }
    return _keeper;
}

// #################################################################################################

Linda* Linda::CreateTimerLinda(lua_State* const L_)
{
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_:
    // Initialize 'timerLinda'; a common Linda object shared by all states
    lua_pushcfunction(L_, LG_linda);                                                               // L_: lanes.linda
    luaG_pushstring(L_, "lanes-timer");                                                            // L_: lanes.linda "lanes-timer"
    lua_pushinteger(L_, 0);                                                                        // L_: lanes.linda "lanes-timer" 0
    lua_call(L_, 2, 1);                                                                            // L_: linda
    STACK_CHECK(L_, 1);

    // Proxy userdata contents is only a 'DeepPrelude*' pointer
    auto const _timerLinda{ *luaG_tofulluserdata<Linda*>(L_, kIdxTop) };
    // increment refcount so that this linda remains alive as long as the universe exists.
    _timerLinda->refcount.fetch_add(1, std::memory_order_relaxed);
    lua_pop(L_, 1);                                                                                // L_:
    STACK_CHECK(L_, 0);
    return _timerLinda;
}

// #################################################################################################

void Linda::DeleteTimerLinda(lua_State* const L_, Linda* const linda_)
{
    if (linda_ != nullptr) { // test in case some early internal error prevented Lanes from creating the deep timer
        [[maybe_unused]] auto const _prev_ref_count{ linda_->refcount.fetch_sub(1, std::memory_order_relaxed) };
        LUA_ASSERT(L_, _prev_ref_count == 1); // this should be the last reference
        DeepFactory::DeleteDeepObject(L_, linda_);
    }
}

// #################################################################################################

void Linda::freeAllocatedName()
{
    if (std::holds_alternative<std::string_view>(nameVariant)) {
        std::string_view& _name = std::get<std::string_view>(nameVariant);
        U->internalAllocator.free(const_cast<char*>(_name.data()), _name.size());
        _name = {};
    }
}

// #################################################################################################

std::string_view Linda::getName() const
{
    if (std::holds_alternative<std::string_view>(nameVariant)) {
        std::string_view const& _name = std::get<std::string_view>(nameVariant);
        return _name;
    }
    if (std::holds_alternative<EmbeddedName>(nameVariant)) {
        EmbeddedName const& _name = std::get<EmbeddedName>(nameVariant);
        return std::string_view{ _name.data() };
    }
    return std::string_view{};
}

// #################################################################################################

// used to perform all linda operations that access keepers
int Linda::ProtectedCall(lua_State* const L_, lua_CFunction const f_)
{
    Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };

    // acquire the keeper
    Keeper* const _keeper{ _linda->acquireKeeper() };
    KeeperState const _K{ _keeper ? _keeper->K : KeeperState{ static_cast<lua_State*>(nullptr) } };
    if (_K == nullptr)
        return 0;

    // no GC allowed during the call, because we don't want to trigger collection of another linda
    // bound to the same keeper, as that would cause a deadlock when trying to acquire it while
    // doing LindaFactory::deleteDeepObjectInternal -> keeper_call(clear)
    lua_gc(L_, LUA_GCSTOP, 0);

    LUA_ASSERT_CODE(auto const _koip{ _linda->startKeeperOperation(L_) });
    // if we didn't do anything wrong, the keeper stack should be clean
    LUA_ASSERT(L_, lua_gettop(_K) == 0);

    // push the function to be called and move it before the arguments
    lua_pushcfunction(L_, f_);
    lua_insert(L_, 1);
    // do a protected call
    LuaError const _rc{ lua_pcall(L_, lua_gettop(L_) - 1, LUA_MULTRET, 0) };
    // whatever happens, the keeper state stack must be empty when we are done
    lua_settop(_K, 0);

    // restore normal GC operations
    lua_gc(L_, LUA_GCRESTART, 0);

    // release the keeper
    _linda->releaseKeeper(_keeper);

    // if there was an error, forward it
    if (_rc != LuaError::OK) {
        raise_lua_error(L_);
    }
    // return whatever the actual operation provided
    return lua_gettop(L_);
}

// #################################################################################################

void Linda::pushCancelString(lua_State* L_) const
{
    luaG_pushstring(L_, cancelStatus == Status::Cancelled ? "cancelled" : "active");
}

// #################################################################################################

void Linda::releaseKeeper(Keeper* const keeper_) const
{
    if (keeper_) { // can be nullptr if we tried to acquire during shutdown
        assert(keeper_ == whichKeeper());
        keeper_->mutex.unlock();
    }
}

// #################################################################################################

void Linda::setName(std::string_view const& name_)
{
    // keep default
    if (name_.empty()) {
        return;
    }

    // if we currently hold an allocated name, free it
    freeAllocatedName();
    if (name_.size() <= kEmbeddedNameLength) {
        // grab our internal buffer
        EmbeddedName& _name = nameVariant.emplace<EmbeddedName>();
        // copy the string in it
        memcpy(_name.data(), name_.data(), name_.size());
    } else {
        // allocate an external buffer
        char* const _nameBuffer{ static_cast<char*>(U->internalAllocator.alloc(name_.size())) };
        // copy the string in it
        memcpy(_nameBuffer, name_.data(), name_.size());
        nameVariant.emplace<std::string_view>(_nameBuffer, name_.size());
    }
}

// #################################################################################################
// #################################################################################################
// ########################################## Lua API ##############################################
// #################################################################################################
// #################################################################################################

/*
 * (void) = linda_cancel( linda_ud, "read"|"write"|"both"|"none")
 *
 * Signal linda so that waiting threads wake up as if their own lane was cancelled
 */
LUAG_FUNC(linda_cancel)
{
    Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
    std::string_view const _who{ luaG_optstring(L_, StackIndex{ 2 }, "both") };
    // make sure we got 2 arguments: the linda and the cancellation mode
    luaL_argcheck(L_, lua_gettop(L_) <= 2, 2, "wrong number of arguments");

    if (_who == "both") { // tell everyone to wake up
        _linda->cancelStatus = Linda::Status::Cancelled;
        _linda->writeHappened.notify_all();
        _linda->readHappened.notify_all();
    } else if (_who == "none") { // reset flag
        _linda->cancelStatus = Linda::Status::Active;
    } else if (_who == "read") { // tell blocked readers to wake up
        _linda->cancelStatus = Linda::Status::Cancelled;
        _linda->writeHappened.notify_all();
    } else if (_who == "write") { // tell blocked writers to wake up
        _linda->cancelStatus = Linda::Status::Cancelled;
        _linda->readHappened.notify_all();
    } else {
        raise_luaL_error(L_, "unknown wake hint '%s'", _who.data());
    }
    return 0;
}

// #################################################################################################

#if LUA_VERSION_NUM >= 504
// linda:__close(err|nil)
static LUAG_FUNC(linda_close)
{
    [[maybe_unused]] Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };                   // L_: linda err|nil

    // do we have a uservalue? it contains a callback
    switch (lua_getiuservalue(L_, 1, 1)) {
    case LUA_TTABLE: // callable table
    case LUA_TUSERDATA: // callable userdata
    case LUA_TFUNCTION:                                                                            // L_: linda err|nil on_close()
        lua_insert(L_, 1);                                                                         // L_: on_close() linda err|nil
        lua_call(L_, lua_gettop(L_) - 1, 0);                                                       // L_:
        return 0;

    case LUA_TNONE:
    case LUA_TNIL:
        return 0;

    default:
        raise_luaL_error(L_, "Invalid __close handler");
    }
}
#endif // LUA_VERSION_NUM >= 504

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
    if (LindaToString<true>(L_, StackIndex{ 1 })) {
        _atLeastOneLinda = true;
        lua_replace(L_, 1);
    }
    if (LindaToString<true>(L_, StackIndex{ 2 })) {
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

// If key is "status" return the linda cancel status
static int linda_index_string(lua_State* L_)
{
    static constexpr StackIndex kIdxSelf{ 1 };
    static constexpr StackIndex kIdxKey{ 2 };

    Linda* const _linda{ ToLinda<false>(L_, kIdxSelf) };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);                                                           // L_: linda "key"

    std::string_view const _keystr{ luaG_tostring(L_, kIdxKey) };
    lua_settop(L_, 2); // keep only our original arguments on the stack

    // look in metatable first
    lua_getmetatable(L_, kIdxSelf);                                                                // L_: linda "key" mt
    lua_replace(L_, -3);                                                                           // L_: mt "key"
    if (luaG_rawget(L_, StackIndex{ -2 }) != LuaType::NIL) { // found something?                   // L_: mt value
        return 1; // done
    }

    lua_pop(L_, 2);                                                                                // L_:
    if (_keystr == "status") {
        _linda->pushCancelString(L_);                                                              // L_: "<status>"
        return 1;
    }
    raise_luaL_error(L_, "unknown field '%s'", _keystr.data());
}

// #################################################################################################

// linda:__index(key,usr) -> value
static LUAG_FUNC(linda_index)
{
    static constexpr StackIndex kIdxKey{ 2 };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);

    switch (luaG_type(L_, kIdxKey)) {
    case LuaType::STRING:
        return linda_index_string(L_); // stack modification is undefined, returned value is at the top

    default: // unknown key
        raise_luaL_error(L_, "Unsupported linda indexing key type %s", luaG_typename(L_, kIdxKey).data());
    }
}

// #################################################################################################

/*
 * (void) = linda_collectgarbage( linda_ud)
 *
 * Force a GC cycle in the keeper assigned to the Linda
 */
LUAG_FUNC(linda_collectgarbage)
{
    static constexpr lua_CFunction _collectgarbage{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            if (lua_gettop(L_) > 1) {
                raise_luaL_argerror(L_, StackIndex{ 2 }, "Unexpected extra argument");
            }
            Keeper* const _keeper{ _linda->whichKeeper() };
            KeeperCallResult const _pushed{ keeper_call(_keeper->K, KEEPER_API(collectgarbage), L_, _linda, StackIndex{ 0 }) };
            return OptionalValue(_pushed, L_, "Unexpected error");
        }
    };
    return Linda::ProtectedCall(L_, _collectgarbage);
}

// #################################################################################################

/*
 * [val] = linda_count( linda_ud, [slot [, ...]])
 *
 * Get a count of the pending elements in the specified keys
 */
LUAG_FUNC(linda_count)
{
    static constexpr lua_CFunction _count{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            // make sure the keys are of a valid type
            CheckKeyTypes(L_, StackIndex{ 2 }, StackIndex{ lua_gettop(L_) });

            Keeper* const _keeper{ _linda->whichKeeper() };
            KeeperCallResult const _pushed{ keeper_call(_keeper->K, KEEPER_API(count), L_, _linda, StackIndex{ 2 }) };
            return OptionalValue(_pushed, L_, "Tried to count an invalid slot");
        }
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
    Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
    lua_pushlightuserdata(L_, _linda->obfuscated<void*>()); // just the address
    return 1;
}

// #################################################################################################

/*
 * table = linda:dump()
 * return a table listing all pending data inside the linda
 */
LUAG_FUNC(linda_dump)
{
    static constexpr lua_CFunction _dump{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            return Keeper::PushLindaStorage(*_linda, DestState{ L_ });
        }
    };
    return Linda::ProtectedCall(L_, _dump);
}

// #################################################################################################

/*
 * count, [val [, ...]]|nil,cancel_error = linda:get(key_num|str|bool|lightuserdata [, count = 1])
 *
 * Get one or more values from Linda.
 */
LUAG_FUNC(linda_get)
{
    static constexpr lua_CFunction _get{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            lua_Integer const _count{ luaL_optinteger(L_, 3, 1) };
            luaL_argcheck(L_, _count >= 1, 3, "count should be >= 1");
            luaL_argcheck(L_, lua_gettop(L_) <= 3, 4, "too many arguments");
            // make sure the slot is of a valid type (throws an error if not the case)
            CheckKeyTypes(L_, StackIndex{ 2 }, StackIndex{ 2 });

            KeeperCallResult _pushed;
            if (_linda->cancelStatus == Linda::Active) {
                Keeper* const _keeper{ _linda->whichKeeper() };
                _pushed = keeper_call(_keeper->K, KEEPER_API(get), L_, _linda, StackIndex{ 2 });
                if (_pushed.has_value() && kRestrictedChannel.equals(L_, kIdxTop)) {
                    raise_luaL_error(L_, "Key is restricted");
                }
            } else { // linda is cancelled
                // do nothing and return nil,lanes.cancel_error
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                _pushed.emplace(2);
            }
            // an error can be raised if we attempt to read an unregistered function
            return OptionalValue(_pushed, L_, "tried to copy unsupported types");
        }
    };
    return Linda::ProtectedCall(L_, _get);
}

// #################################################################################################

/*
 * [bool]|nil,cancel_error = linda:limit(key_num|str|bool|lightuserdata, [int])
 * "unlimited"|number = linda:limit(slot)
 *
 * Read or set limit to 1 Linda keys.
 * Optionally wake threads waiting to write on the linda, in case the limit enables them to do so
 * Limit can be 0 to completely block everything, "unlimited" to reset
 */
LUAG_FUNC(linda_limit)
{
    static constexpr lua_CFunction _limit{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            // make sure we got 2 or 3 arguments: the linda, a slot and optionally a limit
            int const _nargs{ lua_gettop(L_) };
            luaL_argcheck(L_, _nargs == 2 || _nargs == 3, 2, "wrong number of arguments");
            // make sure we got a numeric limit, or "unlimited", (or nothing)
            bool const _unlimited{ luaG_tostring(L_, StackIndex{ 3 }) == "unlimited" };
            LindaLimit const _val{ _unlimited ? std::numeric_limits<LindaLimit::type>::max() : static_cast<LindaLimit::type>(luaL_optinteger(L_, 3, 0)) };
            if (_val < 0) {
                raise_luaL_argerror(L_, StackIndex{ 3 }, "limit must be >= 0");
            }
            // make sure the slot is of a valid type
            CheckKeyTypes(L_, StackIndex{ 2 }, StackIndex{ 2 });

            KeeperCallResult _pushed;
            if (_linda->cancelStatus == Linda::Active) {
                if (_unlimited) {
                    LUA_ASSERT(L_, lua_gettop(L_) == 3 && luaG_tostring(L_, StackIndex{ 3 }) == "unlimited");
                    // inside the Keeper, unlimited is signified with a -1 limit (can't use nil because of nil kNilSentinel conversions!)
                    lua_pop(L_, 1);                                                                // L_: linda slot
                    lua_pushinteger(L_, -1);                                                       // L_: linda slot nil
                }
                Keeper* const _keeper{ _linda->whichKeeper() };
                _pushed = keeper_call(_keeper->K, KEEPER_API(limit), L_, _linda, StackIndex{ 2 });
                LUA_ASSERT(L_, _pushed.has_value() && (_pushed.value() == 2) && luaG_type(L_, kIdxTop) == LuaType::STRING);
                if (_nargs == 3) { // 3 args: setting the limit
                    // changing the limit: no error, boolean value saying if we should wake blocked writer threads
                    LUA_ASSERT(L_, luaG_type(L_, StackIndex{ -2 }) == LuaType::BOOLEAN);           // L_: bool string
                    if (lua_toboolean(L_, -2)) {
                        _linda->readHappened.notify_all(); // To be done from within the 'K' locking area
                    }
                } else { // 2 args: reading the limit
                    // reading the limit: a number >=0 or "unlimited"
                    LUA_ASSERT(L_, luaG_type(L_, StackIndex{ -2 }) == LuaType::NUMBER || luaG_tostring(L_, StackIndex{ -2 }) == "unlimited");
                }
            } else { // linda is cancelled
                // do nothing and return nil,lanes.cancel_error
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                _pushed.emplace(2);
            }
            // propagate returned values
            return _pushed.value();
        }
    };
    return Linda::ProtectedCall(L_, _limit);
}

// #################################################################################################

/*
 * 2 modes of operation
 * [val, slot]= linda:receive([timeout_secs_num=nil], key_num|str|bool|lightuserdata [, ...] )
 * Consumes a single value from the Linda, in any slot.
 * Returns: received value (which is consumed from the slot), and the slot which had it

 * [val1, ... valCOUNT]= linda_receive( linda_ud, [timeout_secs_num=-1], linda.batched, key_num|str|bool|lightuserdata, min_COUNT[, max_COUNT])
 * Consumes between min_COUNT and max_COUNT values from the linda, from a single slot.
 * returns the actual consumed values, or nil if there weren't enough values to consume
 */
LUAG_FUNC(linda_receive)
{
    static constexpr lua_CFunction _receive{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            StackIndex _key_i{ 2 }; // index of first slot, if timeout not there

            std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
            if (luaG_type(L_, StackIndex{ 2 }) == LuaType::NUMBER) { // we don't want to use lua_isnumber() because of autocoercion
                lua_Duration const _duration{ lua_tonumber(L_, 2) };
                if (_duration.count() >= 0.0) {
                    _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(_duration);
                } else {
                    raise_luaL_argerror(L_, StackIndex{ 2 }, "duration cannot be < 0");
                }
                ++_key_i;
            } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the slot
                ++_key_i;
            }

            keeper_api_t _selected_keeper_receive{ nullptr };
            int _expected_pushed_min{ 0 }, _expected_pushed_max{ 0 };
            // are we in batched mode?
            if (kLindaBatched.equals(L_, _key_i)) {
                // no need to pass linda.batched in the keeper state
                ++_key_i;
                // make sure the keys are of a valid type
                CheckKeyTypes(L_, _key_i, _key_i);
                // receive multiple values from a single slot
                _selected_keeper_receive = KEEPER_API(receive_batched);
                // we expect a user-defined amount of return value
                _expected_pushed_min = (int) luaL_checkinteger(L_, _key_i + 1);
                if (_expected_pushed_min < 1) {
                    raise_luaL_argerror(L_, StackIndex{ _key_i + 1 }, "bad min count");
                }
                _expected_pushed_max = (int) luaL_optinteger(L_, _key_i + 2, _expected_pushed_min);
                // don't forget to count the slot in addition to the values
                ++_expected_pushed_min;
                ++_expected_pushed_max;
                if (_expected_pushed_min > _expected_pushed_max) {
                    raise_luaL_argerror(L_, StackIndex{ _key_i + 2 }, "batched min/max error");
                }
            } else {
                // make sure the keys are of a valid type
                CheckKeyTypes(L_, _key_i, StackIndex{ lua_gettop(L_) });
                // receive a single value, checking multiple slots
                _selected_keeper_receive = KEEPER_API(receive);
                // we expect a single (value, slot) pair of returned values
                _expected_pushed_min = _expected_pushed_max = 2;
            }

            Lane* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
            Keeper* const _keeper{ _linda->whichKeeper() };
            KeeperState const _K{ _keeper ? _keeper->K : KeeperState{ static_cast<lua_State*>(nullptr) } };
            if (_K == nullptr)
                return 0;

            CancelRequest _cancel{ CancelRequest::None };
            KeeperCallResult _pushed{};
            STACK_CHECK_START_REL(_K, 0);
            for (bool _try_again{ true };;) {
                if (_lane != nullptr) {
                    _cancel = _lane->cancelRequest.load(std::memory_order_relaxed);
                }
                _cancel = (_cancel != CancelRequest::None)
                    ? _cancel
                    : ((_linda->cancelStatus == Linda::Cancelled) ? CancelRequest::Soft : CancelRequest::None);
                // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
                if (!_try_again || _cancel != CancelRequest::None) {
                    _pushed.emplace(0);
                    break;
                }

                // all arguments of receive() but the first are passed to the keeper's receive function
                _pushed = keeper_call(_K, _selected_keeper_receive, L_, _linda, _key_i);
                if (!_pushed.has_value()) {
                    break;
                }
                if (_pushed.value() > 0) {
                    LUA_ASSERT(L_, _pushed.value() >= _expected_pushed_min && _pushed.value() <= _expected_pushed_max);
                    if (kRestrictedChannel.equals(L_, StackIndex{ kIdxTop })) {
                        raise_luaL_error(L_, "Key is restricted");
                    }
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
                        _prev_status = _lane->status.load(std::memory_order_acquire); // Running, most likely
                        LUA_ASSERT(L_, _prev_status == Lane::Running); // but check, just in case
                        LUA_ASSERT(L_, _lane->waiting_on == nullptr);
                        _lane->waiting_on = &_linda->writeHappened;
                        _lane->status.store(Lane::Waiting, std::memory_order_release);
                    }
                    // not enough data to read: wakeup when data was sent, or when timeout is reached
                    std::unique_lock<std::mutex> _guard{ _keeper->mutex, std::adopt_lock };
                    std::cv_status const _status{ _linda->writeHappened.wait_until(_guard, _until) };
                    _guard.release(); // we don't want to unlock the mutex on exit!
                    _try_again = (_status == std::cv_status::no_timeout); // detect spurious wakeups
                    if (_lane != nullptr) {
                        _lane->waiting_on = nullptr;
                        _lane->status.store(_prev_status, std::memory_order_release);
                    }
                }
            }
            STACK_CHECK(_K, 0);

            if (!_pushed.has_value()) {
                raise_luaL_error(L_, "tried to copy unsupported types");
            }

            switch (_cancel) {
            case CancelRequest::None:
                {
                    int const _nbPushed{ _pushed.value() };
                    if (_nbPushed == 0) {
                        // not enough data in the linda slot to fulfill the request, return nil, "timeout"
                        lua_pushnil(L_);
                        luaG_pushstring(L_, "timeout");
                        return 2;
                    }
                    return _nbPushed;
                }

            case CancelRequest::Soft:
                // if user wants to soft-cancel, the call returns nil, kCancelError
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                return 2;

            case CancelRequest::Hard:
                // raise an error interrupting execution only in case of hard cancel
                raise_cancel_error(L_); // raises an error and doesn't return

            default:
                raise_luaL_error(L_, "internal error: unknown cancel request");
            }
        }
    };
    return Linda::ProtectedCall(L_, _receive);
}

// #################################################################################################

/*
 * "string" = linda:restrict(key_num|str|bool|lightuserdata, [string])
 * "string" = linda:restrict(slot)
 *
 * Read or set restrict mode to 1 Linda slot.
 */
LUAG_FUNC(linda_restrict)
{
    static constexpr lua_CFunction _rstrct{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            // make sure we got 2 or 3 arguments: the linda, a slot and optionally a restrict mode
            int const _nargs{ lua_gettop(L_) };
            luaL_argcheck(L_, _nargs == 2 || _nargs == 3, 2, "wrong number of arguments");
            // make sure we got a known restrict mode, (or nothing)
            std::string_view const _mode{ luaG_tostring(L_, StackIndex{ 3 }) };
            if (!_mode.empty() && (_mode != "none" && _mode != "set/get" && _mode != "send/receive")) {
                raise_luaL_argerror(L_, StackIndex{ 3 }, "unknown restrict mode");
            }
            // make sure the slot is of a valid type
            CheckKeyTypes(L_, StackIndex{ 2 }, StackIndex{ 2 });

            KeeperCallResult _pushed;
            if (_linda->cancelStatus == Linda::Active) {
                Keeper* const _keeper{ _linda->whichKeeper() };
                _pushed = keeper_call(_keeper->K, KEEPER_API(restrict), L_, _linda, StackIndex{ 2 });
                // we should get a single return value: the string describing the previous restrict mode
                LUA_ASSERT(L_, _pushed.has_value() && (_pushed.value() == 1) && luaG_type(L_, kIdxTop) == LuaType::STRING);
            } else { // linda is cancelled
                // do nothing and return nil,lanes.cancel_error
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                _pushed.emplace(2);
            }
            // propagate returned values
            return _pushed.value();
        }
    };
    return Linda::ProtectedCall(L_, _rstrct);
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
    static constexpr lua_CFunction _send{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            StackIndex _key_i{ 2 }; // index of first slot, if timeout not there

            std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
            if (luaG_type(L_, StackIndex{ 2 }) == LuaType::NUMBER) { // we don't want to use lua_isnumber() because of autocoercion
                lua_Duration const _duration{ lua_tonumber(L_, 2) };
                if (_duration.count() >= 0.0) {
                    _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(_duration);
                } else {
                    raise_luaL_argerror(L_, StackIndex{ 2 }, "duration cannot be < 0");
                }
                ++_key_i;
            } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the slot
                ++_key_i;
            }

            // make sure the slot is of a valid type
            CheckKeyTypes(L_, _key_i, _key_i);

            STACK_GROW(L_, 1);

            // make sure there is something to send
            if (lua_gettop(L_) == _key_i) {
                raise_luaL_error(L_, "no data to send");
            }

            bool _ret{ false };
            CancelRequest _cancel{ CancelRequest::None };
            KeeperCallResult _pushed;
            {
                Lane* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
                Keeper* const _keeper{ _linda->whichKeeper() };
                KeeperState const _K{ _keeper ? _keeper->K : KeeperState{ static_cast<lua_State*>(nullptr) } };
                if (_K == nullptr)
                    return 0;

                STACK_CHECK_START_REL(_K, 0);
                for (bool _try_again{ true };;) {
                    if (_lane != nullptr) {
                        _cancel = _lane->cancelRequest.load(std::memory_order_relaxed);
                    }
                    _cancel = (_cancel != CancelRequest::None)
                        ? _cancel
                        : ((_linda->cancelStatus == Linda::Cancelled) ? CancelRequest::Soft : CancelRequest::None);
                    // if user wants to cancel, or looped because of a timeout, the call returns without sending anything
                    if (!_try_again || _cancel != CancelRequest::None) {
                        _pushed.emplace(0);
                        break;
                    }

                    STACK_CHECK(_K, 0);
                    _pushed = keeper_call(_K, KEEPER_API(send), L_, _linda, _key_i);
                    if (!_pushed.has_value()) {
                        break;
                    }
                    LUA_ASSERT(L_, _pushed.value() == 1);

                    if (kRestrictedChannel.equals(L_, StackIndex{ kIdxTop })) {
                        raise_luaL_error(L_, "Key is restricted");
                    }
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
                            _prev_status = _lane->status.load(std::memory_order_acquire); // Running, most likely
                            LUA_ASSERT(L_, _prev_status == Lane::Running); // but check, just in case
                            LUA_ASSERT(L_, _lane->waiting_on == nullptr);
                            _lane->waiting_on = &_linda->readHappened;
                            _lane->status.store(Lane::Waiting, std::memory_order_release);
                        }
                        // could not send because no room: wait until some data was read before trying again, or until timeout is reached
                        std::unique_lock<std::mutex> _guard{ _keeper->mutex, std::adopt_lock };
                        std::cv_status const status{ _linda->readHappened.wait_until(_guard, _until) };
                        _guard.release(); // we don't want to unlock the mutex on exit!
                        _try_again = (status == std::cv_status::no_timeout); // detect spurious wakeups
                        if (_lane != nullptr) {
                            _lane->waiting_on = nullptr;
                            _lane->status.store(_prev_status, std::memory_order_release);
                        }
                    }
                }
                STACK_CHECK(_K, 0);
            }

            if (!_pushed.has_value()) {
                raise_luaL_error(L_, "tried to copy unsupported types");
            }

            switch (_cancel) {
            case CancelRequest::Soft:
                // if user wants to soft-cancel, the call returns nil, kCancelError
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                return 2;

            case CancelRequest::Hard:
                // raise an error interrupting execution only in case of hard cancel
                raise_cancel_error(L_); // raises an error and doesn't return

            default:
                if (_ret) {
                    lua_pushboolean(L_, _ret); // true (success)
                    return 1;
                } else {
                    // not enough room in the Linda slot to fulfill the request, return nil, "timeout"
                    lua_pushnil(L_);
                    luaG_pushstring(L_, "timeout");
                    return 2;
                }
            }
        }
    };
    return Linda::ProtectedCall(L_, _send);
}

// #################################################################################################

/*
 * (boolean,string)|(nil,lanes.cancel_error) = linda:set(key_num|str|bool|lightuserdata [, value [, ...]])
 *
 * Set one or more value to Linda. Ignores limits.
 *
 * Existing slot value is replaced, and possible queued entries removed.
 */
LUAG_FUNC(linda_set)
{
    static constexpr lua_CFunction _set{
        +[](lua_State* const L_) {
            Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
            bool const _has_data{ lua_gettop(L_) > 2 };
            // make sure the slot is of a valid type (throws an error if not the case)
            CheckKeyTypes(L_, StackIndex{ 2 }, StackIndex{ 2 });

            KeeperCallResult _pushed;
            if (_linda->cancelStatus == Linda::Active) {
                Keeper* const _keeper{ _linda->whichKeeper() };
                _pushed = keeper_call(_keeper->K, KEEPER_API(set), L_, _linda, StackIndex{ 2 });
                if (_pushed.has_value()) { // no error?
                    if (kRestrictedChannel.equals(L_, kIdxTop)) {
                        raise_luaL_error(L_, "Key is restricted");
                    }
                    LUA_ASSERT(L_, _pushed.value() == 2 && luaG_type(L_, kIdxTop) == LuaType::STRING && luaG_type(L_, StackIndex{ -2 }) == LuaType::BOOLEAN);

                    if (_has_data) {
                        // we put some data in the slot, tell readers that they should wake
                        _linda->writeHappened.notify_all(); // To be done from within the 'K' locking area
                    }
                    if (lua_toboolean(L_, -2)) {
                        // the slot was full, but it is no longer the case, tell writers they should wake
                        _linda->readHappened.notify_all(); // To be done from within the 'K' locking area
                    }
                }
            } else { // linda is cancelled
                // do nothing and return nil,lanes.cancel_error
                lua_pushnil(L_);
                kCancelError.pushKey(L_);
                _pushed.emplace(2);
            }

            // must trigger any error after keeper state has been released
            return OptionalValue(_pushed, L_, "tried to copy unsupported types");
        }
    };
    return Linda::ProtectedCall(L_, _set);
}

// #################################################################################################

LUAG_FUNC(linda_tostring)
{
    return LindaToString<false>(L_, StackIndex{ 1 });
}

// #################################################################################################

#if HAVE_DECODA_SUPPORT()
/*
 * table/string = linda:__towatch()
 * return a table listing all pending data inside the linda, or the stringified linda if empty
 */
LUAG_FUNC(linda_towatch)
{
    Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
    int _pushed{ Keeper::PushLindaStorage(*_linda, DestState{ L_ }) };
    if (_pushed == 0) {
        // if the linda is empty, don't return nil
        _pushed = LindaToString<false>(L_, StackIndex{ 1 });
    }
    return _pushed;
}

#endif // HAVE_DECODA_SUPPORT()

// #################################################################################################

/*
 * (void) = linda_wake( linda_ud, "read"|"write"|"both")
 *
 * Signal linda so that waiting threads wake up as if their own lane was cancelled
 */
LUAG_FUNC(linda_wake)
{
    Linda* const _linda{ ToLinda<false>(L_, StackIndex{ 1 }) };
    std::string_view const _who{ luaG_optstring(L_, StackIndex{ 2 }, "both") };
    // make sure we got 2 arguments: the linda and the wake targets
    luaL_argcheck(L_, lua_gettop(L_) <= 2, 2, "wrong number of arguments");

    if (_who == "both") { // tell everyone to wake up
        _linda->writeHappened.notify_all();
        _linda->readHappened.notify_all();
    } else if (_who == "read") { // simulate a read to wake writers
        _linda->writeHappened.notify_all();
    } else if (_who == "write") { // simulate a write to wake readers
        _linda->readHappened.notify_all();
    } else {
        raise_luaL_error(L_, "unknown wake hint '%s'", _who.data());
    }
    return 0;
}

// #################################################################################################

namespace {
    namespace local {
        static luaL_Reg const sLindaMT[] = {
#if LUA_VERSION_NUM >= 504
            { "__close", LG_linda_close },
#endif // LUA_VERSION_NUM >= 504
            { "__concat", LG_linda_concat },
            { "__index", LG_linda_index },
            { "__tostring", LG_linda_tostring },
#if HAVE_DECODA_SUPPORT()
            { "__towatch", LG_linda_towatch }, // Decoda __towatch support
#endif // HAVE_DECODA_SUPPORT()
            { "cancel", LG_linda_cancel },
            { "collectgarbage", LG_linda_collectgarbage },
            { "count", LG_linda_count },
            { "deep", LG_linda_deep },
            { "dump", LG_linda_dump },
            { "get", LG_linda_get },
            { "limit", LG_linda_limit },
            { "receive", LG_linda_receive },
            { "restrict", LG_linda_restrict },
            { "send", LG_linda_send },
            { "set", LG_linda_set },
            { "wake", LG_linda_wake },
            { nullptr, nullptr }
        };
    } // namespace local
} // namespace
// it's somewhat awkward to instanciate the LindaFactory here instead of lindafactory.cpp,
// but that's necessary to provide s_LindaMT without exposing it outside linda.cpp.
/*static*/ LindaFactory LindaFactory::Instance{ local::sLindaMT };

// #################################################################################################
// #################################################################################################

/*
 * ud = lanes.linda( [name[,group[,close_handler]]])
 *
 * returns a linda object, or raises an error if creation failed
 */
LUAG_FUNC(linda)
{
    static constexpr StackIndex kLastArg{ LUA_VERSION_NUM >= 504 ? 3 : 2 };
    StackIndex const _top{ lua_gettop(L_) };
    luaL_argcheck(L_, _top <= kLastArg, _top, "too many arguments");
    StackIndex _closeHandlerIdx{};
    StackIndex _nameIdx{};
    StackIndex _groupIdx{};
    for (StackIndex const _i : std::ranges::iota_view{ StackIndex{ 1 }, StackIndex{ _top + 1 }}) {
        switch (luaG_type(L_, _i)) {
#if LUA_VERSION_NUM >= 504 // to-be-closed support starts with Lua 5.4
        case LuaType::FUNCTION:
            luaL_argcheck(L_, _closeHandlerIdx == 0, _i, "More than one __close handler");
            _closeHandlerIdx = _i;
            break;

        case LuaType::USERDATA:
        case LuaType::TABLE:
            luaL_argcheck(L_, _closeHandlerIdx == 0, _i, "More than one __close handler");
            luaL_argcheck(L_, luaL_getmetafield(L_, _i, "__call") != 0, _i, "__close handler is not callable");
            lua_pop(L_, 1); // luaL_getmetafield() pushed the field, we need to pop it
            _closeHandlerIdx = _i;
            break;
#endif // LUA_VERSION_NUM >= 504

        case LuaType::STRING:
            luaL_argcheck(L_, _nameIdx == 0, _i, "More than one name");
            _nameIdx = _i;
            break;

        case LuaType::NUMBER:
            luaL_argcheck(L_, _groupIdx == 0, _i, "More than one group");
            _groupIdx = _i;
            break;

        default:
            luaL_argcheck(L_, false, _i, "Bad argument type (should be a string, a number, or a callable type)");
        }
    }
 
    int const _nbKeepers{ Universe::Get(L_)->keepers.getNbKeepers() };
    if (!_groupIdx) {
        luaL_argcheck(L_, _nbKeepers < 2, 0, "Group is mandatory in multiple Keeper scenarios");
    } else {
        int const _group{ static_cast<int>(lua_tointeger(L_, _groupIdx)) };
        luaL_argcheck(L_, _group >= 0 && _group < _nbKeepers, _groupIdx, "Group out of range");
    }

    // done with argument checking, let's proceed
    if constexpr (LUA_VERSION_NUM >= 504) {
        // make sure we have kMaxArgs arguments on the stack for processing, with name, group, and handler, in that order
        lua_settop(L_, kLastArg);                                                                  // L_: a b c
        // If either index is 0, lua_settop() adjusted the stack with a nil in slot kLastArg
        lua_pushvalue(L_, _closeHandlerIdx ? _closeHandlerIdx : kLastArg);                         // L_: a b c close_handler
        lua_pushvalue(L_, _groupIdx ? _groupIdx : kLastArg);                                       // L_: a b c close_handler group
        lua_pushvalue(L_, _nameIdx ? _nameIdx : kLastArg);                                         // L_: a b c close_handler group name
        lua_replace(L_, 1);                                                                        // L_: name b c close_handler group
        lua_replace(L_, 2);                                                                        // L_: name group c close_handler
        lua_replace(L_, 3);                                                                        // L_: name group close_handler

        // if we have a __close handler, we need a uservalue slot to store it
        UserValueCount const _nuv{ _closeHandlerIdx ? 1 : 0 };
        LindaFactory::Instance.pushDeepUserdata(DestState{ L_ }, _nuv);                            // L_: name group close_handler linda
        if (_closeHandlerIdx != 0) {
            lua_replace(L_, 2);                                                                    // L_: name linda close_handler
            lua_setiuservalue(L_, StackIndex{ 2 }, UserValueIndex{ 1 });                           // L_: name linda
        }
        // depending on whether we have a handler or not, the stack is not in the same state at this point
        // just make sure we have our Linda at the top
        LUA_ASSERT(L_, ToLinda<true>(L_, kIdxTop));
        return 1;
    } else { // no to-be-closed support
        // ensure we have name, group in that order on the stack
        if (_nameIdx > _groupIdx) {
            lua_insert(L_, 1);                                                                     // L_: name group
        }
        LindaFactory::Instance.pushDeepUserdata(DestState{ L_ }, UserValueCount{ 0 });             // L_: name group linda
        return 1;
    }

}
