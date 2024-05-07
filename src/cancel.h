#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "macros_and_utils.h"
#include "uniquekey.h"

// #################################################################################################

class Lane; // forward

/*
 * Lane cancellation request modes
 */
enum class CancelRequest
{
    None, // no pending cancel request
    Soft, // user wants the lane to cancel itself manually on cancel_test()
    Hard // user wants the lane to be interrupted (meaning code won't return from those functions) from inside linda:send/receive calls
};

enum class CancelResult
{
    Timeout,
    Cancelled
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

// xxh64 of string "kCancelError" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kCancelError{ 0x0630345FEF912746ull, "lanes.cancel_error" }; // 'raise_cancel_error' sentinel

[[nodiscard]] CancelOp which_cancel_op(char const* opString_);
[[nodiscard]] CancelResult thread_cancel(Lane* lane_, CancelOp op_, int hookCount_, std::chrono::time_point<std::chrono::steady_clock> until_, bool wakeLane_);

[[noreturn]] static inline void raise_cancel_error(lua_State* L_)
{
    STACK_GROW(L_, 1);
    kCancelError.pushKey(L_); // special error value
    raise_lua_error(L_);
}

// #################################################################################################
// #################################################################################################

LUAG_FUNC(cancel_test);
LUAG_FUNC(thread_cancel);

// #################################################################################################
