#pragma once

#include "macros_and_utils.hpp"
#include "uniquekey.hpp"

// #################################################################################################

// Lane cancellation request modes
enum class [[nodiscard]] CancelRequest : uint8_t
{
    None, // no pending cancel request
    Soft, // user wants the lane to cancel itself manually on cancel_test()
    Hard // user wants the lane to be interrupted (meaning code won't return from those functions) from inside linda:send/receive calls
};

struct [[nodiscard]] CancelOp
{
    CancelRequest mode;
    LuaHookMask hookMask;
};

enum class [[nodiscard]] CancelResult : uint8_t
{
    Timeout,
    Cancelled
};

// xxh64 of string "kCancelError" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kCancelError{ 0x0630345FEF912746ull, "lanes.cancel_error" }; // 'raise_cancel_error' sentinel

// #################################################################################################

[[nodiscard]]
CancelRequest CheckCancelRequest(lua_State* L_);

// #################################################################################################

[[noreturn]]
static inline void raise_cancel_error(lua_State* const L_)
{
    STACK_GROW(L_, 1);
    kCancelError.pushKey(L_); // special error value
    raise_lua_error(L_);
}

// #################################################################################################

LUAG_FUNC(cancel_test);
LUAG_FUNC(lane_cancel);
