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
#include "_pch.h"
#include "cancel.h"

#include "debugspew.h"
#include "lane.h"

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
[[nodiscard]] CancelRequest CheckCancelRequest(lua_State* const L_)
{
    Lane* const _lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
    // 'lane' is nullptr for the original main state (and no-one can cancel that)
    return _lane ? _lane->cancelRequest : CancelRequest::None;
}

// #################################################################################################
// #################################################################################################

//---
// = thread_cancel( lane_ud [,timeout_secs=0.0] [,wake_lindas_bool=false] )
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

// #################################################################################################
// #################################################################################################

CancelOp WhichCancelOp(std::string_view const& opString_)
{
    CancelOp _op{ CancelOp::Invalid };
    if (opString_ == "hard") {
        _op = CancelOp::Hard;
    } else if (opString_ == "soft") {
        _op = CancelOp::Soft;
    } else if (opString_== "call") {
        _op = CancelOp::MaskCall;
    } else if (opString_ == "ret") {
        _op = CancelOp::MaskRet;
    } else if (opString_ == "line") {
        _op = CancelOp::MaskLine;
    } else if (opString_ == "count") {
        _op = CancelOp::MaskCount;
    } else if (opString_ == "all") {
        _op = CancelOp::MaskAll;
    }
    return _op;
}

// #################################################################################################

[[nodiscard]] static CancelOp WhichCancelOp(lua_State* const L_, int const idx_)
{
    if (luaG_type(L_, idx_) == LuaType::STRING) {
        std::string_view const _str{ luaG_tostring(L_, idx_) };
        CancelOp _op{ WhichCancelOp(_str) };
        lua_remove(L_, idx_); // argument is processed, remove it
        if (_op == CancelOp::Invalid) {
            raise_luaL_error(L_, "invalid hook option %s", _str);
        }
        return _op;
    }
    return CancelOp::Hard;
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
    CancelRequest _test{ CheckCancelRequest(L_) };
    lua_pushboolean(L_, _test != CancelRequest::None);
    return 1;
}

// #################################################################################################

// bool[,reason] = lane_h:cancel( [mode, hookcount] [, timeout] [, wake_lane])
LUAG_FUNC(thread_cancel)
{
    Lane* const _lane{ ToLane(L_, 1) };
    CancelOp const _op{ WhichCancelOp(L_, 2) }; // this removes the op string from the stack

    int _hook_count{ 0 };
    if (static_cast<int>(_op) > static_cast<int>(CancelOp::Soft)) { // hook is requested
        _hook_count = static_cast<int>(luaL_checkinteger(L_, 2));
        lua_remove(L_, 2); // argument is processed, remove it
        if (_hook_count < 1) {
            raise_luaL_error(L_, "hook count cannot be < 1");
        }
    }

    std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
    if (luaG_type(L_, 2) == LuaType::NUMBER) { // we don't want to use lua_isnumber() because of autocoercion
        lua_Duration const duration{ lua_tonumber(L_, 2) };
        if (duration.count() >= 0.0) {
            _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        } else {
            raise_luaL_argerror(L_, 2, "duration cannot be < 0");
        }
        lua_remove(L_, 2); // argument is processed, remove it
    } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
        lua_remove(L_, 2); // argument is processed, remove it
    }

    // we wake by default in "hard" mode (remember that hook is hard too), but this can be turned off if desired
    bool _wake_lane{ _op != CancelOp::Soft };
    if (lua_gettop(L_) >= 2) {
        if (!lua_isboolean(L_, 2)) {
            raise_luaL_error(L_, "wake_lindas argument is not a boolean");
        }
        _wake_lane = lua_toboolean(L_, 2);
        lua_remove(L_, 2); // argument is processed, remove it
    }
    STACK_CHECK_START_REL(L_, 0);
    switch (_lane->cancel(_op, _hook_count, _until, _wake_lane)) {
    default: // should never happen unless we added a case and forgot to handle it
        raise_luaL_error(L_, "should not get here!");
        break;

    case CancelResult::Timeout:
        lua_pushboolean(L_, 0);                                                                    // false
        lua_pushstring(L_, "timeout");                                                             // false "timeout"
        break;

    case CancelResult::Cancelled:
        lua_pushboolean(L_, 1);                                                                    // true
        _lane->pushStatusString(L_);                                                               // true "<status>"
        break;
    }
    STACK_CHECK(L_, 2);
    return 2;
}
