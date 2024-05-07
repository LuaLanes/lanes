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

#include "cancel.h"

#include "lanes_private.h"
#include "threading.h"
#include "tools.h"

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
[[nodiscard]] static inline CancelRequest cancel_test(lua_State* L_)
{
    Lane* const lane{ kLanePointerRegKey.readLightUserDataValue<Lane>(L_) };
    // 'lane' is nullptr for the original main state (and no-one can cancel that)
    return lane ? lane->cancelRequest : CancelRequest::None;
}

// #################################################################################################

//---
// bool = cancel_test()
//
// Available inside the global namespace of lanes
// returns a boolean saying if a cancel request is pending
//
LUAG_FUNC(cancel_test)
{
    CancelRequest test{ cancel_test(L_) };
    lua_pushboolean(L_, test != CancelRequest::None);
    return 1;
}

// #################################################################################################
// #################################################################################################

[[nodiscard]] static void cancel_hook(lua_State* L_, [[maybe_unused]] lua_Debug* ar_)
{
    DEBUGSPEW_CODE(fprintf(stderr, "cancel_hook\n"));
    if (cancel_test(L_) != CancelRequest::None) {
        lua_sethook(L_, nullptr, 0, 0);
        raise_cancel_error(L_);
    }
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

[[nodiscard]] static CancelResult thread_cancel_soft(Lane* lane_, std::chrono::time_point<std::chrono::steady_clock> until_, bool wakeLane_)
{
    lane_->cancelRequest = CancelRequest::Soft; // it's now signaled to stop
    // negative timeout: we don't want to truly abort the lane, we just want it to react to cancel_test() on its own
    if (wakeLane_) { // wake the thread so that execution returns from any pending linda operation if desired
        std::condition_variable* const waiting_on{ lane_->waiting_on };
        if (lane_->status == Lane::Waiting && waiting_on != nullptr) {
            waiting_on->notify_all();
        }
    }

    return lane_->waitForCompletion(until_) ? CancelResult::Cancelled : CancelResult::Timeout;
}

// #################################################################################################

[[nodiscard]] static CancelResult thread_cancel_hard(Lane* lane_, std::chrono::time_point<std::chrono::steady_clock> until_, bool wakeLane_)
{
    lane_->cancelRequest = CancelRequest::Hard; // it's now signaled to stop
    // lane_->thread.get_stop_source().request_stop();
    if (wakeLane_) { // wake the thread so that execution returns from any pending linda operation if desired
        std::condition_variable* waiting_on = lane_->waiting_on;
        if (lane_->status == Lane::Waiting && waiting_on != nullptr) {
            waiting_on->notify_all();
        }
    }

    CancelResult result{ lane_->waitForCompletion(until_) ? CancelResult::Cancelled : CancelResult::Timeout };
    return result;
}

// #################################################################################################

CancelResult thread_cancel(Lane* lane_, CancelOp op_, int hookCount_, std::chrono::time_point<std::chrono::steady_clock> until_, bool wakeLane_)
{
    // remember that lanes are not transferable: only one thread can cancel a lane, so no multithreading issue here
    // We can read 'lane_->status' without locks, but not wait for it (if Posix no PTHREAD_TIMEDJOIN)
    if (lane_->status >= Lane::Done) {
        // say "ok" by default, including when lane is already done
        return CancelResult::Cancelled;
    }

    // signal the linda the wake up the thread so that it can react to the cancel query
    // let us hope we never land here with a pointer on a linda that has been destroyed...
    if (op_ == CancelOp::Soft) {
        return thread_cancel_soft(lane_, until_, wakeLane_);
    } else if (static_cast<int>(op_) > static_cast<int>(CancelOp::Soft)) {
        lua_sethook(lane_->L, cancel_hook, static_cast<int>(op_), hookCount_);
    }

    return thread_cancel_hard(lane_, until_, wakeLane_);
}

// #################################################################################################
// #################################################################################################

CancelOp which_cancel_op(char const* opString_)
{
    CancelOp op{ CancelOp::Invalid };
    if (strcmp(opString_, "hard") == 0) {
        op = CancelOp::Hard;
    } else if (strcmp(opString_, "soft") == 0) {
        op = CancelOp::Soft;
    } else if (strcmp(opString_, "call") == 0) {
        op = CancelOp::MaskCall;
    } else if (strcmp(opString_, "ret") == 0) {
        op = CancelOp::MaskRet;
    } else if (strcmp(opString_, "line") == 0) {
        op = CancelOp::MaskLine;
    } else if (strcmp(opString_, "count") == 0) {
        op = CancelOp::MaskCount;
    }
    return op;
}

// #################################################################################################

[[nodiscard]] static CancelOp which_cancel_op(lua_State* L_, int idx_)
{
    if (lua_type(L_, idx_) == LUA_TSTRING) {
        char const* const str{ lua_tostring(L_, idx_) };
        CancelOp op{ which_cancel_op(str) };
        lua_remove(L_, idx_); // argument is processed, remove it
        if (op == CancelOp::Invalid) {
            raise_luaL_error(L_, "invalid hook option %s", str);
        }
        return op;
    }
    return CancelOp::Hard;
}

// #################################################################################################

// bool[,reason] = lane_h:cancel( [mode, hookcount] [, timeout] [, wake_lane])
LUAG_FUNC(thread_cancel)
{
    Lane* const lane{ ToLane(L_, 1) };
    CancelOp const op{ which_cancel_op(L_, 2) }; // this removes the op string from the stack

    int hook_count{ 0 };
    if (static_cast<int>(op) > static_cast<int>(CancelOp::Soft)) { // hook is requested
        hook_count = static_cast<int>(luaL_checkinteger(L_, 2));
        lua_remove(L_, 2); // argument is processed, remove it
        if (hook_count < 1) {
            raise_luaL_error(L_, "hook count cannot be < 1");
        }
    }

    std::chrono::time_point<std::chrono::steady_clock> until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
    if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
        lua_Duration const duration{ lua_tonumber(L_, 2) };
        if (duration.count() >= 0.0) {
            until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        } else {
            raise_luaL_argerror(L_, 2, "duration cannot be < 0");
        }
        lua_remove(L_, 2); // argument is processed, remove it
    } else if (lua_isnil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
        lua_remove(L_, 2); // argument is processed, remove it
    }

    // we wake by default in "hard" mode (remember that hook is hard too), but this can be turned off if desired
    bool wake_lane{ op != CancelOp::Soft };
    if (lua_gettop(L_) >= 2) {
        if (!lua_isboolean(L_, 2)) {
            raise_luaL_error(L_, "wake_lindas parameter is not a boolean");
        }
        wake_lane = lua_toboolean(L_, 2);
        lua_remove(L_, 2); // argument is processed, remove it
    }
    STACK_CHECK_START_REL(L_, 0);
    switch (thread_cancel(lane, op, hook_count, until, wake_lane)) {
    default: // should never happen unless we added a case and forgot to handle it
        LUA_ASSERT(L_, false);
        break;

    case CancelResult::Timeout:
        lua_pushboolean(L_, 0); // false
        lua_pushstring(L_, "timeout"); // false "timeout"
        break;

    case CancelResult::Cancelled:
        lua_pushboolean(L_, 1); // true
        lane->pushThreadStatus(L_); // true status
        break;
    }
    STACK_CHECK(L_, 2);
    return 2;
}
