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

typedef struct s_Lane Lane; // forward

/*
 * Lane cancellation request modes
 */
enum e_cancel_request
{
    CANCEL_NONE, // no pending cancel request
    CANCEL_SOFT, // user wants the lane to cancel itself manually on cancel_test()
    CANCEL_HARD  // user wants the lane to be interrupted (meaning code won't return from those functions) from inside linda:send/receive calls
};

typedef enum
{
    CR_Timeout,
    CR_Cancelled,
    CR_Killed
} cancel_result;

typedef enum
{
    CO_Invalid = -2,
    CO_Hard = -1,
    CO_Soft = 0,
    CO_Count = LUA_MASKCOUNT,
    CO_Line = LUA_MASKLINE,
    CO_Call = LUA_MASKCALL,
    CO_Ret = LUA_MASKRET,
} CancelOp;

// crc64/we of string "CANCEL_ERROR" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey CANCEL_ERROR{ 0xe97d41626cc97577ull }; // 'cancel_error' sentinel

// crc64/we of string "CANCEL_TEST_KEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey CANCEL_TEST_KEY{ 0xe66f5960c57d133aull }; // used as registry key

cancel_result thread_cancel( lua_State* L, Lane* s, CancelOp op_, double secs_, bool force_, double waitkill_timeout_);

static inline int cancel_error( lua_State* L)
{
    STACK_GROW( L, 1);
    CANCEL_ERROR.push(L); // special error value
    return lua_error( L); // doesn't return
}

// ################################################################################################
// ################################################################################################

LUAG_FUNC( cancel_test);
LUAG_FUNC( thread_cancel);

// ################################################################################################
