/*
--
-- CANCEL.CPP
--
-- Lane cancellation support
--
-- Author: Benoit Germain <bnt.germain@gmail.com>
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
#include "cancel.hpp"

#include "debugspew.hpp"
#include "lane.hpp"

namespace {
    namespace local {

        // #########################################################################################
        // #########################################################################################

        [[nodiscard]]
        static std::optional<CancelOp> WhichCancelOp(std::string_view const& opString_)
        {
            if (opString_ == "soft") {
                return std::make_optional<CancelOp>(CancelRequest::Soft, LuaHookMask::None);
            } else if (opString_ == "hard") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::None);
            } else if (opString_ == "call") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::Call);
            } else if (opString_ == "ret") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::Ret);
            } else if (opString_ == "line") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::Line);
            } else if (opString_ == "count") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::Count);
            } else if (opString_ == "all") {
                return std::make_optional<CancelOp>(CancelRequest::Hard, LuaHookMask::All);
            }
            return std::nullopt;
        }

        // #########################################################################################

        [[nodiscard]]
        static CancelOp WhichCancelOp(lua_State* const L_, StackIndex const idx_)
        {
            if (luaW_type(L_, idx_) == LuaType::STRING) {
                std::string_view const _str{ luaW_tostring(L_, idx_) };
                auto const _op{ WhichCancelOp(_str) };
                lua_remove(L_, idx_); // argument is processed, remove it
                if (!_op.has_value()) {
                    raise_luaL_error(L_, "Invalid cancel operation '%s'", _str.data());
                }
                return _op.value();
            }
            return CancelOp{ CancelRequest::Hard, LuaHookMask::None };
        }

    } // namespace local
} // namespace

// #################################################################################################
// #################################################################################################

/*
 * Check if the thread in question ('L') has been signalled for cancel.
 *
 * Called by cancellation hooks and/or pending Linda operations (because then
 * the check won't affect performance).
 *
 * Returns CANCEL_SOFT/HARD if any locks are to be exited, and 'raise_cancel_error()' called,
 * to make execution of the lane end.
 */
[[nodiscard]]
CancelRequest CheckCancelRequest(lua_State* const L_)
{
    auto const* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
    // 'lane' is nullptr for the original main state (and no-one can cancel that)
    return _lane ? _lane->cancelRequest.load(std::memory_order_relaxed) : CancelRequest::None;
}

// #################################################################################################
// #################################################################################################
// ######################################### Lua API ###############################################
// #################################################################################################
// #################################################################################################

//---
// bool = cancel_test()
//
// Available inside the global namespace of a lane
// returns a boolean saying if a cancel request is pending
//
LUAG_FUNC(cancel_test)
{
    CancelRequest const _test{ CheckCancelRequest(L_) };
    if (_test == CancelRequest::None) {
        lua_pushboolean(L_, 0);
    } else {
        luaW_pushstring(L_, (_test == CancelRequest::Soft) ? "soft" : "hard");
    }
    return 1;
}

// #################################################################################################

//---
// = lane_cancel( lane_ud [,timeout_secs=0.0] [,wake_lindas_bool=false] )
//
// The originator thread asking us specifically to cancel the other thread.
//
// 'timeout': <0: wait forever, until the lane is finished
//            0.0: just signal it to cancel, no time waited
//            >0: time to wait for the lane to detect cancellation
//
// 'wake_lindas_bool': if true, signal any linda the thread is waiting on
//                     instead of waiting for its timeout (if any)
//
// Returns: true if the lane was already finished (Done/Error/Cancelled) or if we
//          managed to cancel it.
//          false if the cancellation timed out, or a kill was needed.
//

// bool[,reason] = lane_h:cancel( [cancel_op, hookcount] [, timeout] [, wake_lane])
LUAG_FUNC(lane_cancel)
{
    Lane* const _lane{ ToLane(L_, StackIndex{ 1 }) };                                              // L_: lane [cancel_op, hookcount] [, timeout] [, wake_lane]
    CancelOp const _op{ local::WhichCancelOp(L_, StackIndex{ 2 }) };                               // L_: lane [hookcount] [, timeout] [, wake_lane]

    int const _hook_count{ std::invoke([_op, L_]() {
        if (_op.hookMask == LuaHookMask::None) {
            // the caller shouldn't have provided a hook count in that case
            return 0;
        }
        if (luaW_type(L_, StackIndex{ 2 }) != LuaType::NUMBER) {
            raise_luaL_error(L_, "Hook count expected");
        }
        auto const _hook_count{ static_cast<int>(lua_tointeger(L_, 2)) };
        lua_remove(L_, 2); // argument is processed, remove it                                     // L_: lane [timeout] [, wake_lane]
        if (_hook_count < 1) {
            raise_luaL_error(L_, "Hook count cannot be < 1");
        }
        return _hook_count;
    }) };

    std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
    if (luaW_type(L_, StackIndex{ 2 }) == LuaType::NUMBER) { // we don't want to use lua_isnumber() because of autocoercion
        lua_Duration const duration{ lua_tonumber(L_, 2) };
        if (duration.count() >= 0.0) {
            _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        } else {
            raise_luaL_error(L_, "Duration cannot be < 0");
        }
        lua_remove(L_, 2); // argument is processed, remove it                                     // L_: lane [wake_lane]
    } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil
        lua_remove(L_, 2); // argument is processed, remove it                                     // L_: lane [wake_lane]
    }

    // we wake by default in "hard" mode (remember that hook is hard too), but this can be turned off if desired
    WakeLane _wake_lane{ (_op.mode == CancelRequest::Hard) ? WakeLane::Yes : WakeLane::No };
    if (lua_gettop(L_) >= 2) {
        if (!lua_isboolean(L_, 2)) {
            raise_luaL_error(L_, "Boolean expected for wake_lane argument, got %s", luaW_typename(L_, StackIndex{ 2 }).data());
        }
        _wake_lane = lua_toboolean(L_, 2) ? WakeLane::Yes : WakeLane::No;
        lua_remove(L_, 2); // argument is processed, remove it                                     // L_: lane
    }

    // if the caller didn't fumble, we should have removed everything from the stack but the lane itself
    if (lua_gettop(L_) > 1) {
        raise_luaL_error(L_, "Too many arguments");
    }
    lua_pop(L_, 1);                                                                                // L_:

    STACK_CHECK_START_ABS(L_, 0);
    switch (_lane->cancel(_op, _until, _wake_lane, _hook_count)) {
    default: // should never happen unless we added a case and forgot to handle it
        raise_luaL_error(L_, "Should not get here!");
        break;

    case CancelResult::Timeout:
        lua_pushboolean(L_, 0);                                                                    // L_: false
        luaW_pushstring(L_, "timeout");                                                            // L_: false "timeout"
        break;

    case CancelResult::Cancelled:
        lua_pushboolean(L_, 1);                                                                    // L_: true
        _lane->pushStatusString(L_);                                                               // L_: true "<status>"
        break;
    }
    STACK_CHECK(L_, 2);
    return 2;
}
