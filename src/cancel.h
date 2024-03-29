#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "uniquekey.h"
#include "macros_and_utils.h"

// ################################################################################################

class Lane; // forward

/*
 * Lane cancellation request modes
 */
enum class CancelRequest
{
    None, // no pending cancel request
    Soft, // user wants the lane to cancel itself manually on cancel_test()
    Hard  // user wants the lane to be interrupted (meaning code won't return from those functions) from inside linda:send/receive calls
};

enum class CancelResult
{
    Timeout,
    Cancelled,
    Killed
};

enum class CancelOp
{
    Invalid = -2,
    Hard = -1,
    Soft = 0,
    MaskCall = LUA_MASKCALL,
    MaskRet = LUA_MASKRET,
    MaskLine = LUA_MASKLINE,
    MaskCount = LUA_MASKCOUNT,
};

// crc64/we of string "CANCEL_ERROR" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey CANCEL_ERROR{ 0xe97d41626cc97577ull }; // 'raise_cancel_error' sentinel

CancelResult thread_cancel(lua_State* L, Lane* lane_, CancelOp op_, double secs_, bool force_, double waitkill_timeout_);

[[noreturn]] static inline void raise_cancel_error(lua_State* L)
{
    STACK_GROW(L, 1);
    CANCEL_ERROR.pushKey(L); // special error value
    raise_lua_error(L); // doesn't return
}

// ################################################################################################
// ################################################################################################

LUAG_FUNC(cancel_test);
LUAG_FUNC(thread_cancel);

// ################################################################################################
