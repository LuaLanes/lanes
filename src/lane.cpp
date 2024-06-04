/*
===============================================================================

Copyright (C) 2024 Benoit Germain <bnt.germain@gmail.com>

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

#include "lane.h"

#include "debugspew.h"
#include "intercopycontext.h"
#include "threading.h"
#include "tools.h"

// #################################################################################################

// xxh64 of string "error" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kCachedError{ 0xD6F35DD608D0A203ull };
// xxh64 of string "tostring" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kCachedTostring{ 0xAB5EA23BCEA0C35Cull };

// #################################################################################################
// ######################################### Lua API ###############################################
// #################################################################################################

static LUAG_FUNC(get_debug_threadname)
{
    Lane* const _lane{ ToLane(L_, 1) };
    luaL_argcheck(L_, lua_gettop(L_) == 1, 2, "too many arguments");
    std::ignore = lua_pushstringview(L_, _lane->debugName);
    return 1;
}

// #################################################################################################

// void= finalizer( finalizer_func )
//
// finalizer_func( [err, stack_tbl] )
//
// Add a function that will be called when exiting the lane, either via
// normal return or an error.
//
static LUAG_FUNC(set_finalizer)
{
    luaL_argcheck(L_, lua_isfunction(L_, 1), 1, "finalizer should be a function");
    luaL_argcheck(L_, lua_gettop(L_) == 1, 1, "too many arguments");
    STACK_GROW(L_, 3);
    // Get the current finalizer table (if any), create one if it doesn't exist
    std::ignore = kFinalizerRegKey.getSubTable(L_, 1, 0);                                          // L_: finalizer {finalisers}
    // must cast to int, not lua_Integer, because LuaJIT signature of lua_rawseti is not the same as PUC-Lua.
    int const _idx{ static_cast<int>(lua_rawlen(L_, -1) + 1) };
    lua_pushvalue(L_, 1);                                                                          // L_: finalizer {finalisers} finalizer
    lua_rawseti(L_, -2, _idx);                                                                     // L_: finalizer {finalisers}
    // no need to adjust the stack, Lua does this for us
    return 0;
}

// #################################################################################################

// upvalue #1 is the lane userdata
static LUAG_FUNC(set_debug_threadname)
{
    // C s_lane structure is a light userdata upvalue
    Lane* const _lane{ lua_tolightuserdata<Lane>(L_, lua_upvalueindex(1)) };
    LUA_ASSERT(L_, L_ == _lane->L); // this function is exported in a lane's state, therefore it is callable only from inside the Lane's state
    lua_settop(L_, 1);
    STACK_CHECK_START_REL(L_, 0);
    _lane->changeDebugName(-1);
    STACK_CHECK(L_, 0);
    return 0;
}

// #################################################################################################

//---
// [...] | [nil, err_any, stack_tbl]= thread_join( lane_ud [, wait_secs=-1] )
//
//  timeout:   returns nil
//  done:      returns return values (0..N)
//  error:     returns nil + error value [+ stack table]
//  cancelled: returns nil
//
static LUAG_FUNC(thread_join)
{
    Lane* const _lane{ ToLane(L_, 1) };

    std::chrono::time_point<std::chrono::steady_clock> _until{ std::chrono::time_point<std::chrono::steady_clock>::max() };
    if (lua_type(L_, 2) == LUA_TNUMBER) { // we don't want to use lua_isnumber() because of autocoercion
        lua_Duration const duration{ lua_tonumber(L_, 2) };
        if (duration.count() >= 0.0) {
            _until = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        } else {
            raise_luaL_argerror(L_, 2, "duration cannot be < 0");
        }

    } else if (!lua_isnoneornil(L_, 2)) { // alternate explicit "infinite timeout" by passing nil before the key
        raise_luaL_argerror(L_, 2, "incorrect duration type");
    }

    bool const _done{ !_lane->thread.joinable() || _lane->waitForCompletion(_until) };
    lua_settop(L_, 1);                                                                             // L_: lane
    lua_State* const _L2{ _lane->L };
    if (!_done || !_L2) {
        lua_pushnil(L_);                                                                           // L_: lane nil
        lua_pushliteral(L_, "timeout");                                                            // L_: lane nil "timeout"
        return 2;
    }

    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: lane
    // Thread is Done/Error/Cancelled; all ours now

    int _ret{ 0 };
    // debugName is a pointer to string possibly interned in the lane's state, that no longer exists when the state is closed
    // so store it in the userdata uservalue at a key that can't possibly collide
    _lane->securizeDebugName(L_);
    switch (_lane->status) {
    case Lane::Done:
        {
            int const _n{ lua_gettop(_L2) }; // whole L2 stack
            if (
                (_n > 0) &&
                (InterCopyContext{ _lane->U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} }.inter_move(_n) != InterCopyResult::Success)
            ) {                                                                                    // L_: lane results                                L2:
                raise_luaL_error(L_, "tried to copy unsupported types");
            }
            _ret = _n;
        }
        break;

    case Lane::Error:
        {
            int const _n{ lua_gettop(_L2) };                                                       // L_: lane                                        L2: "err" [trace]
            STACK_GROW(L_, 3);
            lua_pushnil(L_);                                                                       // L_: lane nil
            // even when _lane->errorTraceLevel != Minimal, if the error is not LUA_ERRRUN, the handler wasn't called, and we only have 1 error message on the stack ...
            InterCopyContext _c{ _lane->U, DestState{ L_ }, SourceState{ _L2 }, {}, {}, {}, {}, {} };
            if (_c.inter_move(_n) != InterCopyResult::Success) {                                   // L_: lane nil "err" [trace]                      L2:
                raise_luaL_error(L_, "tried to copy unsupported types: %s", lua_tostring(L_, -_n));
            }
            _ret = 1 + _n;
        }
        break;

    case Lane::Cancelled:
        _ret = 0;
        break;

    default:
        DEBUGSPEW_CODE(DebugSpew(nullptr) << "Unknown Lane status: " << static_cast<int>(_lane->status) << std::endl);
        LUA_ASSERT(L_, false);
        _ret = 0;
    }
    _lane->close();
    STACK_CHECK(L_, _ret);
    return _ret;
}

// #################################################################################################

// key is numeric, wait until the thread returns and populate the environment with the return values
// If the return values signal an error, propagate it
// Else If key is found in the environment, return it
static int thread_index_number(lua_State* L_)
{
    static constexpr int kSelf{ 1 };
    static constexpr int kKey{ 2 };
    static constexpr int kUsr{ 3 };

    Lane* const _lane{ ToLane(L_, kSelf) };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);                                                           // L_: lane n

    // first, check that we don't already have an environment that holds the requested value
    // If key is found in the uservalue, return it
    lua_getiuservalue(L_, kSelf, 1);                                                               // L_: lane n {uv}
    lua_pushvalue(L_, kKey);                                                                       // L_: lane n {uv} n
    lua_rawget(L_, kUsr);                                                                          // L_: lane n {uv} v|nil
    if (!lua_isnil(L_, -1)) {
        return 1;
    }
    lua_pop(L_, 1);                                                                                // L_: lane n {uv}

    // check if we already fetched the values from the thread or not
    lua_pushinteger(L_, 0);                                                                        // L_: lane n {uv} 0
    lua_rawget(L_, kUsr);                                                                          // L_: lane n {uv} uv[0]|nil
    bool const _fetched{ !lua_isnil(L_, -1) };
    lua_pop(L_, 1);                                                                                // L_: lane n {uv}
    if (!_fetched) {
        lua_pushinteger(L_, 0);                                                                    // L_: lane n {uv} 0
        lua_pushboolean(L_, 1);                                                                    // L_: lane n {uv} 0 true
        lua_rawset(L_, kUsr);                                                                      // L_: lane n {uv}
        // wait until thread has completed, transfer everything from the lane's stack to our side
        lua_pushcfunction(L_, LG_thread_join);                                                     // L_: lane n {uv} join
        lua_pushvalue(L_, kSelf);                                                                  // L_: lane n {uv} join lane
        lua_call(L_, 1, LUA_MULTRET); // lane:join()                                               // L_: lane n {uv} ...
        switch (_lane->status) {
        default:
            // this is an internal error, we probably never get here
            lua_settop(L_, 0);                                                                     // L_:
            lua_pushliteral(L_, "Unexpected status: ");                                            // L_: "Unexpected status: "
            std::ignore = _lane->pushThreadStatus(L_);                                             // L_: "Unexpected status: " "<status>"
            lua_concat(L_, 2);                                                                     // L_: "Unexpected status: <status>"
            raise_lua_error(L_);

        case Lane::Done: // got regular return values
            {
                int const _nvalues{ lua_gettop(L_) - 3 };                                          // L_: lane n {uv} ...
                for (int _i = _nvalues; _i > 0; --_i) {
                    // pop the last element of the stack, to store it in the uservalue at its proper index
                    lua_rawseti(L_, kUsr, _i);                                                     // L_: lane n {uv}
                }
            }
            break;

        case Lane::Error: // got 2 or 3 values: nil, errstring, and possibly a callstack table
            if (_lane->errorTraceLevel == Lane::Minimal) {
                LUA_ASSERT(L_, lua_gettop(L_) == 5 && lua_isnil(L_, 4) && !lua_isnil(L_, 5));      // L_: lane n {uv} nil "<msg>"
            } else {
                LUA_ASSERT(L_, lua_gettop(L_) == 6 && lua_isnil(L_, 4) && !lua_isnil(L_, 5) && lua_istable(L_, 6));
                lua_insert(L_, -2);                                                                // L_: lane n {uv} nil {trace} "<msg>"
            }
            // uv[-1] = "<msg>"
            lua_rawseti(L_, kUsr, -1);                                                             // L_: lane n {uv} nil {trace}?
            break;

        case Lane::Cancelled:
            // do nothing
            break;
        }
    }
    STACK_GROW(L_, 6); // up to 6 positions are needed in case of error propagation
    lua_settop(L_, 3);                                                                             // L_: lane n {uv}
    int const _key{ static_cast<int>(lua_tointeger(L_, kKey)) };
    if (_key != -1) {
        lua_rawgeti(L_, kUsr, -1);                                                                 // L_: lane n {uv} <error>|nil
        if (!lua_isnil(L_, -1)) { // an error was stored                                           // L_: lane n {uv} <error>
            lua_getmetatable(L_, 1);                                                               // L_: lane n {uv} <error> {mt}
            lua_replace(L_, -3);                                                                   // L_: lane n {mt} <error>
            // Note: Lua 5.1 interpreter is not prepared to show
            //       non-string errors, so we use 'tostring()' here
            //       to get meaningful output.  --AKa 22-Jan-2009
            //
            //       Also, the stack dump we get is no good; it only
            //       lists our internal Lanes functions. There seems
            //       to be no way to switch it off, though.
            //
            // Level 3 should show the line where 'h[x]' was read
            // but this only seems to work for string messages
            // (Lua 5.1.4). No idea, why.   --AKa 22-Jan-2009
            if constexpr (LUA_VERSION_NUM == 501) {
                if (!lua_isstring(L_, -1)) {
                    kCachedTostring.pushKey(L_);                                                   // L_: lane n {mt} <error> kCachedTostring
                    lua_rawget(L_, -3);                                                            // L_: lane n {mt} <error> tostring()
                    lua_insert(L_, -2);                                                            // L_: lane n {mt} tostring() <error>
                    lua_call(L_, 1, 1); // tostring(errstring)                                     // L_: lane n {mt} "error"
                }
            }
            kCachedError.pushKey(L_);                                                              // L_: lane n {mt} "error" kCachedError
            lua_rawget(L_, -3);                                                                    // L_: lane n {mt} "error" error()
            lua_replace(L_, -3);                                                                   // L_: lane n error() "error"
            lua_pushinteger(L_, 3);                                                                // L_: lane n error() "error" 3
            lua_call(L_, 2, 0); // error(tostring(errstring), 3) -> doesn't return                 // L_: lane n
            raise_luaL_error(L_, "%s: should not get here!", _lane->debugName.data());
        } else {
            lua_pop(L_, 1);                                                                        // L_: lane n {uv}
        }
    }
    lua_rawgeti(L_, kUsr, _key);                                                                   // L_: lane n {uv} uv[n]
    return 1;
}

// #################################################################################################

// If key is "status" return the thread status
// If key is found in the environment, return it
// Else raise an error
static int thread_index_string(lua_State* L_)
{
    static constexpr int kSelf{ 1 };
    static constexpr int kKey{ 2 };

    Lane* const _lane{ ToLane(L_, kSelf) };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);                                                           // L_: lane "key"

    std::string_view const _keystr{ lua_tostringview(L_, kKey) };
    lua_settop(L_, 2); // keep only our original arguments on the stack
    if (_keystr == "status") {
        std::ignore = _lane->pushThreadStatus(L_);                                                 // L_: lane "key" "<status>"
        return 1;
    }
    if (_keystr == "error_trace_level") {
        std::ignore = _lane->pushErrorTraceLevel(L_);                                              // L_: lane "key" "<level>"
        return 1;
    }
    // return self.metatable[key]
    lua_getmetatable(L_, kSelf);                                                                   // L_: lane "key" mt
    lua_replace(L_, -3);                                                                           // L_: mt "key"
    lua_rawget(L_, -2);                                                                            // L_: mt value
    // only "cancel" and "join" are registered as functions, any other string will raise an error
    if (!lua_iscfunction(L_, -1)) {
        raise_luaL_error(L_, "can't index a lane with '%s'", _keystr.data());
    }
    return 1;
}

// #################################################################################################

// lane:__index(key,usr) -> value
static LUAG_FUNC(thread_index)
{
    static constexpr int kSelf{ 1 };
    static constexpr int kKey{ 2 };
    Lane* const _lane{ ToLane(L_, kSelf) };
    LUA_ASSERT(L_, lua_gettop(L_) == 2);

    switch (lua_type(L_, kKey)) {
    case LUA_TNUMBER:
        return thread_index_number(L_); // stack modification is undefined, returned value is at the top

    case LUA_TSTRING:
        return thread_index_string(L_); // stack modification is undefined, returned value is at the top

    default: // unknown key
        lua_getmetatable(L_, kSelf);                                                               // L_: mt
        std::ignore = luaG_getfield(L_, -1, "cached_error");                                       // L_: mt error
        lua_pushliteral(L_, "Unknown key: ");                                                      // L_: mt error "Unknown key: "
        lua_pushvalue(L_, kKey);                                                                   // L_: mt error "Unknown key: " k
        lua_concat(L_, 2);                                                                         // L_: mt error "Unknown key: <k>"
        lua_call(L_, 1, 0); // error( "Unknown key: " .. key) -> doesn't return                    // L_: mt
        raise_luaL_error(L_, "%s[%s]: should not get here!", _lane->debugName.data(), lua_typename(L_, lua_type(L_, kKey)));
    }
}

// #################################################################################################
// ######################################## Utilities ##############################################
// #################################################################################################

#if USE_DEBUG_SPEW()
namespace {
    // can't use direct LUA_x errcode indexing because the sequence is not the same between Lua 5.1 and 5.2 :-(
    // LUA_ERRERR doesn't have the same value
    struct errcode_name
    {
        LuaError code;
        std::string_view const name;
    };

    namespace local {
        static struct errcode_name sErrCodes[] = {
            { LuaError::OK, "LUA_OK" },
            { LuaError::YIELD, "LUA_YIELD" },
            { LuaError::ERRRUN, "LUA_ERRRUN" },
            { LuaError::ERRSYNTAX, "LUA_ERRSYNTAX" },
            { LuaError::ERRMEM, "LUA_ERRMEM" },
            { LuaError::ERRGCMM, "LUA_ERRGCMM" },
            { LuaError::ERRERR, "LUA_ERRERR" },
            { LuaError::ERRFILE, "LUA_ERRFILE" },
        };
    } // namespace local

    static std::string_view GetErrcodeName(LuaError _code) noexcept
    {
        for (errcode_name const& _entry : local::sErrCodes) {
            if (_entry.code == _code) {
                return _entry.name;
            }
        }
        return "<nullptr>";
    }
} // namespace
#endif // USE_DEBUG_SPEW()

// #################################################################################################

/*
 * str= lane_error( error_val|str )
 *
 * Called if there's an error in some lane; add call stack to error message
 * just like 'lua.c' normally does.
 *
 * ".. will be called with the error message and its return value will be the
 *     message returned on the stack by lua_pcall."
 *
 * Note: Rather than modifying the error message itself, it would be better
 *     to provide the call stack (as string) completely separated. This would
 *     work great with non-string error values as well (current system does not).
 *     (This is NOT possible with the Lua 5.1 'lua_pcall()'; we could of course
 *     implement a Lanes-specific 'pcall' of our own that does this). TBD!!! :)
 *       --AKa 22-Jan-2009
 */

// xxh64 of string "kStackTraceRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kStackTraceRegKey{ 0x3F327747CACAA904ull };

[[nodiscard]] static int lane_error(lua_State* L_)
{
    // error message (any type)
    STACK_CHECK_START_ABS(L_, 1); // L_: some_error

    // Don't do stack survey for cancelled lanes.
    //
    if (kCancelError.equals(L_, 1)) {
        return 1; // just pass on
    }

    STACK_GROW(L_, 3);
    bool const _extended{ kExtendedStackTraceRegKey.readBoolValue(L_) };
    STACK_CHECK(L_, 1);

    // Place stack trace at 'registry[kStackTraceRegKey]' for the 'lua_pcall()'
    // caller to fetch. This bypasses the Lua 5.1 limitation of only one
    // return value from error handler to 'lua_pcall()' caller.

    // It's adequate to push stack trace as a table. This gives the receiver
    // of the stack best means to format it to their liking. Also, it allows
    // us to add more stack info later, if needed.
    //
    // table of { "sourcefile.lua:<line>", ... }
    //
    lua_newtable(L_);                                                                              // L_: some_error {}

    // Best to start from level 1, but in some cases it might be a C function
    // and we don't get '.currentline' for that. It's okay - just keep level
    // and table index growing separate.    --AKa 22-Jan-2009
    //
    lua_Debug _ar;
    for (int _n = 1; lua_getstack(L_, _n, &_ar); ++_n) {
        lua_getinfo(L_, _extended ? "Sln" : "Sl", &_ar);
        if (_extended) {
            lua_newtable(L_);                                                                      // L_: some_error {} {}

            lua_pushstring(L_, _ar.source);                                                        // L_: some_error {} {} source
            lua_setfield(L_, -2, "source");                                                        // L_: some_error {} {}

            lua_pushinteger(L_, _ar.currentline);                                                  // L_: some_error {} {} currentline
            lua_setfield(L_, -2, "currentline");                                                   // L_: some_error {} {}

            lua_pushstring(L_, _ar.name);                                                          // L_: some_error {} {} name
            lua_setfield(L_, -2, "name");                                                          // L_: some_error {} {}

            lua_pushstring(L_, _ar.namewhat);                                                      // L_: some_error {} {} namewhat
            lua_setfield(L_, -2, "namewhat");                                                      // L_: some_error {} {}

            lua_pushstring(L_, _ar.what);                                                          // L_: some_error {} {} what
            lua_setfield(L_, -2, "what");                                                          // L_: some_error {} {}
        } else if (_ar.currentline > 0) {
            lua_pushfstring(L_, "%s:%d", _ar.short_src, _ar.currentline);                          // L_: some_error {} "blah:blah"
        } else {
            lua_pushfstring(L_, "%s:?", _ar.short_src);                                            // L_: some_error {} "blah"
        }
        lua_rawseti(L_, -2, static_cast<lua_Integer>(_n));                                         // L_: some_error {}
    }

    // store the stack trace table in the registry
    kStackTraceRegKey.setValue(L_, [](lua_State* L_) { lua_insert(L_, -2); });                     // L_: some_error

    STACK_CHECK(L_, 1);
    return 1; // the untouched error value
}

// #################################################################################################
// ########################################## Finalizer ############################################
// #################################################################################################

static void push_stack_trace(lua_State* L_, Lane::ErrorTraceLevel errorTraceLevel_, LuaError rc_, [[maybe_unused]] int stk_base_)
{
    // Lua 5.1 error handler is limited to one return value; it stored the stack trace in the registry
    switch (rc_) {
    case LuaError::OK: // no error, body return values are on the stack
        break;

    case LuaError::ERRRUN: // cancellation or a runtime error
        if (errorTraceLevel_ != Lane::Minimal) { // when not Minimal, we installed a handler
            STACK_CHECK_START_REL(L_, 0);
            // fetch the call stack table from the registry where the handler stored it
            STACK_GROW(L_, 1);
            // yields nil if no stack was generated (in case of cancellation for example)
            kStackTraceRegKey.pushValue(L_);                                                       // L_: err trace|nil
            STACK_CHECK(L_, 1);

            // For cancellation the error message is kCancelError, and a stack trace isn't placed
            // For other errors, the message can be whatever was thrown, and we should have a stack trace table
            LUA_ASSERT(L_, lua_type(L_, 1 + stk_base_) == (kCancelError.equals(L_, stk_base_) ? LUA_TNIL : LUA_TTABLE));
            // Just leaving the stack trace table on the stack is enough to get it through to the master.
        } else {
            // any kind of error can be thrown with error(), or through a lane/linda cancellation
            LUA_ASSERT(L_, lua_gettop(L_) == stk_base_);
        }
        break;

    case LuaError::ERRMEM: // memory allocation error (handler not called)
    case LuaError::ERRERR: // error while running the error handler (if any, for example an out-of-memory condition)
    default:
        // the Lua core provides a string error message in those situations
        LUA_ASSERT(L_, (lua_gettop(L_) == stk_base_) && (lua_type(L_, stk_base_) == LUA_TSTRING));
        break;
    }
}

// #################################################################################################
//---
// Run finalizers - if any - with the given parameters
//
// If 'rc' is nonzero, error message and stack index (the latter only when errorTraceLevel_ == 1) are available as:
//      [-1]: stack trace (table)
//      [-2]: error message (any type)
//
// Returns:
//      0 if finalizers were run without error (or there were none)
//      LUA_ERRxxx return code if any of the finalizers failed
//
// TBD: should we add stack trace on failing finalizer, wouldn't be hard..
//

[[nodiscard]] static LuaError run_finalizers(lua_State* L_, Lane::ErrorTraceLevel errorTraceLevel_, LuaError lua_rc_)
{
    kFinalizerRegKey.pushValue(L_);                                                                // L_: ... finalizers?
    if (lua_isnil(L_, -1)) {
        lua_pop(L_, 1);
        return LuaError::OK; // no finalizers
    }

    STACK_GROW(L_, 5);

    int const _finalizers_index{ lua_gettop(L_) };
    int const _err_handler_index{ (errorTraceLevel_ != Lane::Minimal) ? (lua_pushcfunction(L_, lane_error), lua_gettop(L_)) : 0 };

    LuaError _rc{ LuaError::OK };
    for (int _n = static_cast<int>(lua_rawlen(L_, _finalizers_index)); _n > 0; --_n) {
        int _args{ 0 };
        lua_pushinteger(L_, _n);                                                                   // L_: ... finalizers lane_error n
        lua_rawget(L_, _finalizers_index);                                                         // L_: ... finalizers lane_error finalizer
        LUA_ASSERT(L_, lua_isfunction(L_, -1));
        if (lua_rc_ != LuaError::OK) { // we have an error message and an optional stack trace at the bottom of the stack
            LUA_ASSERT(L_, _finalizers_index == 2 || _finalizers_index == 3);
            //std::string_view const _err_msg{ lua_tostringview(L_, 1) };
            lua_pushvalue(L_, 1);                                                                  // L_: ... finalizers lane_error finalizer err_msg
            // note we don't always have a stack trace for example when kCancelError, or when we got an error that doesn't call our handler, such as LUA_ERRMEM
            if (_finalizers_index == 3) {
                lua_pushvalue(L_, 2); // L_: ... finalizers lane_error finalizer err_msg stack_trace
            }
            _args = _finalizers_index - 1;
        }

        // if no error from the main body, finalizer doesn't receive any argument, else it gets the error message and optional stack trace
        _rc = ToLuaError(lua_pcall(L_, _args, 0, _err_handler_index));                             // L_: ... finalizers lane_error err_msg2?
        if (_rc != LuaError::OK) {
            push_stack_trace(L_, errorTraceLevel_, _rc, lua_gettop(L_));                           // L_: ... finalizers lane_error err_msg2? trace
            // If one finalizer fails, don't run the others. Return this
            // as the 'real' error, replacing what we could have had (or not)
            // from the actual code.
            break;
        }
        // no error, proceed to next finalizer                                                     // L_: ... finalizers lane_error
    }

    if (_rc != LuaError::OK) {
        // errorTraceLevel_ accounts for the presence of lane_error on the stack
        int const _nb_err_slots{ lua_gettop(L_) - _finalizers_index - ((errorTraceLevel_ != Lane::Minimal) ? 1 : 0) };
        // a finalizer generated an error, this is what we leave of the stack
        for (int _n = _nb_err_slots; _n > 0; --_n) {
            lua_replace(L_, _n);
        }
        // leave on the stack only the error and optional stack trace produced by the error in the finalizer
        lua_settop(L_, _nb_err_slots);                                                             // L_: ... lane_error trace
    } else { // no error from the finalizers, make sure only the original return values from the lane body remain on the stack
        lua_settop(L_, _finalizers_index - 1);
    }

    return _rc;
}

// #################################################################################################

/*
 * Add the lane to selfdestruct chain; the ones still running at the end of the
 * whole process will be cancelled.
 */
static void selfdestruct_add(Lane* lane_)
{
    std::lock_guard<std::mutex> _guard{ lane_->U->selfdestructMutex };
    assert(lane_->selfdestruct_next == nullptr);

    lane_->selfdestruct_next = lane_->U->selfdestructFirst;
    lane_->U->selfdestructFirst = lane_;
}

// #################################################################################################

// A free-running lane has ended; remove it from selfdestruct chain
[[nodiscard]] static bool selfdestruct_remove(Lane* lane_)
{
    bool _found{ false };
    std::lock_guard<std::mutex> _guard{ lane_->U->selfdestructMutex };
    // Make sure (within the MUTEX) that we actually are in the chain
    // still (at process exit they will remove us from chain and then
    // cancel/kill).
    //
    if (lane_->selfdestruct_next != nullptr) {
        Lane* volatile* _ref = static_cast<Lane* volatile*>(&lane_->U->selfdestructFirst);

        while (*_ref != SELFDESTRUCT_END) {
            if (*_ref == lane_) {
                *_ref = lane_->selfdestruct_next;
                lane_->selfdestruct_next = nullptr;
                // the terminal shutdown should wait until the lane is done with its lua_close()
                lane_->U->selfdestructingCount.fetch_add(1, std::memory_order_release);
                _found = true;
                break;
            }
            _ref = static_cast<Lane* volatile*>(&((*_ref)->selfdestruct_next));
        }
        assert(_found);
    }
    return _found;
}

// #################################################################################################
// ########################################## Main #################################################
// #################################################################################################

static void PrepareLaneHelpers(Lane* lane_)
{
    lua_State* const _L{ lane_->L };
    // Tie "set_finalizer()" to the state
    lua_pushcfunction(_L, LG_set_finalizer);
    tools::PopulateFuncLookupTable(_L, -1, "set_finalizer");
    lua_setglobal(_L, "set_finalizer");

    // Tie "set_debug_threadname()" to the state
    // But don't register it in the lookup database because of the Lane pointer upvalue
    lua_pushlightuserdata(_L, lane_);
    lua_pushcclosure(_L, LG_set_debug_threadname, 1);
    lua_setglobal(_L, "set_debug_threadname");

    // Tie "cancel_test()" to the state
    lua_pushcfunction(_L, LG_cancel_test);
    tools::PopulateFuncLookupTable(_L, -1, "cancel_test");
    lua_setglobal(_L, "cancel_test");
}

// #################################################################################################

static void lane_main(Lane* lane_)
{
    lua_State* const _L{ lane_->L };
    // wait until the launching thread has finished preparing L
    lane_->ready.wait();
    LuaError _rc{ LuaError::ERRRUN };
    if (lane_->status == Lane::Pending) { // nothing wrong happened during preparation, we can work
        // At this point, the lane function and arguments are on the stack, possibly preceded by the error handler
        int const _errorHandlerCount{ lane_->errorTraceLevel == Lane::Minimal ? 0 : 1};
        int const _nargs{ lua_gettop(_L) - 1 - _errorHandlerCount };
        DEBUGSPEW_CODE(Universe* _U = Universe::Get(_L));
        lane_->status = Lane::Running; // Pending -> Running

        PrepareLaneHelpers(lane_);

        _rc = ToLuaError(lua_pcall(_L, _nargs, LUA_MULTRET, _errorHandlerCount));                  // L: eh? retvals|err

        if (_errorHandlerCount) {
            lua_remove(_L, 1);                                                                     // L: retvals|error
        }

        // in case of error and if it exists, fetch stack trace from registry and push it
        push_stack_trace(_L, lane_->errorTraceLevel, _rc, 1);                                      // L: retvals|error [trace]

        DEBUGSPEW_CODE(DebugSpew(_U) << "Lane " << _L << " body: " << GetErrcodeName(_rc) << " (" << (kCancelError.equals(_L, 1) ? "cancelled" : lua_typename(_L, lua_type(_L, 1))) << ")" << std::endl);
        //  Call finalizers, if the script has set them up.
        //
        LuaError const _rc2{ run_finalizers(_L, lane_->errorTraceLevel, _rc) };
        DEBUGSPEW_CODE(DebugSpew(_U) << "Lane " << _L << " finalizer: " << GetErrcodeName(_rc2) << std::endl);
        if (_rc2 != LuaError::OK) { // Error within a finalizer!
            // the finalizer generated an error, and left its own error message [and stack trace] on the stack
            _rc = _rc2; // we're overruling the earlier script error or normal return
        }
        lane_->waiting_on = nullptr;  // just in case
        if (selfdestruct_remove(lane_)) { // check and remove (under lock!)
            // We're a free-running thread and no-one's there to clean us up.
            lane_->close();
            lane_->U->selfdestructMutex.lock();
            // done with lua_close(), terminal shutdown sequence may proceed
            lane_->U->selfdestructingCount.fetch_sub(1, std::memory_order_release);
            lane_->U->selfdestructMutex.unlock();

            // we destroy our jthread member from inside the thread body, so we have to detach so that we don't try to join, as this doesn't seem a good idea
            lane_->thread.detach();
            delete lane_;
            lane_ = nullptr;
        }
    }
    if (lane_) {
        // leave results (1..top) or error message + stack trace (1..2) on the stack - master will copy them

        Lane::Status const _st{ (_rc == LuaError::OK) ? Lane::Done : kCancelError.equals(_L, 1) ? Lane::Cancelled : Lane::Error };

        {
            // 'doneMutex' protects the -> Done|Error|Cancelled state change
            std::lock_guard _guard{ lane_->doneMutex };
            lane_->status = _st;
            lane_->doneCondVar.notify_one(); // wake up master (while 'lane_->doneMutex' is on)
        }
    }
}

// #################################################################################################

// = thread_gc( lane_ud )
//
// Cleanup for a thread userdata. If the thread is still executing, leave it
// alive as a free-running thread (will clean up itself).
//
// * Why NOT cancel/kill a loose thread: 
//
// At least timer system uses a free-running thread, they should be handy
// and the issue of canceling/killing threads at gc is not very nice, either
// (would easily cause waits at gc cycle, which we don't want).
//
[[nodiscard]] static int lane_gc(lua_State* L_)
{
    bool _have_gc_cb{ false };
    Lane* const _lane{ ToLane(L_, 1) };                                                             // L_: ud

    // if there a gc callback?
    lua_getiuservalue(L_, 1, 1);                                                                   // L_: ud uservalue
    kLaneGC.pushKey(L_);                                                                           // L_: ud uservalue __gc
    lua_rawget(L_, -2);                                                                            // L_: ud uservalue gc_cb|nil
    if (!lua_isnil(L_, -1)) {
        lua_remove(L_, -2);                                                                        // L_: ud gc_cb|nil
        std::ignore = lua_pushstringview(L_, _lane->debugName);                                    // L_: ud gc_cb name
        _have_gc_cb = true;
    } else {
        lua_pop(L_, 2);                                                                            // L_: ud
    }

    // We can read 'lane->status' without locks, but not wait for it
    if (_lane->status < Lane::Done) {
        // still running: will have to be cleaned up later
        selfdestruct_add(_lane);
        assert(_lane->selfdestruct_next);
        if (_have_gc_cb) {
            lua_pushliteral(L_, "selfdestruct");                                                   // L_: ud gc_cb name status
            lua_call(L_, 2, 0);                                                                    // L_: ud
        }
        return 0;
    } else if (_lane->L) {
        // no longer accessing the Lua VM: we can close right now
        _lane->close();
        // just in case, but _lane will be freed soon so...
        _lane->debugName = std::string_view{ "<gc>" };
    }

    // Clean up after a (finished) thread
    delete _lane;

    // do this after lane cleanup in case the callback triggers an error
    if (_have_gc_cb) {
        lua_pushliteral(L_, "closed");                                                             // L_: ud gc_cb name status
        lua_call(L_, 2, 0);                                                                        // L_: ud
    }
    return 0;
}

// #################################################################################################
// #################################### Lane implementation ########################################
// #################################################################################################

Lane::Lane(Universe* U_, lua_State* L_, ErrorTraceLevel errorTraceLevel_)
: U{ U_ }
, L{ L_ }
, errorTraceLevel{ errorTraceLevel_ }
{
    assert(errorTraceLevel == ErrorTraceLevel::Minimal || errorTraceLevel == ErrorTraceLevel::Basic || errorTraceLevel == ErrorTraceLevel::Extended);
    kExtendedStackTraceRegKey.setValue(L_, [yes = errorTraceLevel == ErrorTraceLevel::Extended ? 1 : 0](lua_State* L_) { lua_pushboolean(L_, yes); });
    U->tracker.tracking_add(this);
}

// #################################################################################################

Lane::~Lane()
{
    std::ignore = U->tracker.tracking_remove(this);
}

// #################################################################################################

void Lane::changeDebugName(int const nameIdx_)
{
    int const _nameIdx{ luaG_absindex(L, nameIdx_) };
    luaL_checktype(L, _nameIdx, LUA_TSTRING);                                                      // L: ... "name" ...
    STACK_CHECK_START_REL(L, 0);
    // store a hidden reference in the registry to make sure the string is kept around even if a lane decides to manually change the "decoda_name" global...
    kLaneNameRegKey.setValue(L, [idx = _nameIdx](lua_State* L_) { lua_pushvalue(L_, idx); });      // L: ... "name" ...
    // keep a direct pointer on the string
    debugName = lua_tostringview(L, _nameIdx);
    if constexpr (HAVE_DECODA_SUPPORT()) {
        // to see VM name in Decoda debugger Virtual Machine window
        lua_pushvalue(L, _nameIdx);                                                                // L: ... "name" ... "name"
        lua_setglobal(L, "decoda_name");                                                           // L: ... "name" ...
    }
    // and finally set the OS thread name
    THREAD_SETNAME(debugName.data());
    STACK_CHECK(L, 0);
}

// #################################################################################################

//---
// str= thread_status( lane )
//
// Returns: "pending"   not started yet
//          -> "running"   started, doing its work..
//             <-> "waiting"   blocked in a receive()
//                -> "done"     finished, results are there
//                   / "error"     finished at an error, error value is there
//                   / "cancelled"   execution cancelled by M (state gone)
//
[[nodiscard]] std::string_view Lane::errorTraceLevelString() const
{
    std::string_view const _str{
        (errorTraceLevel == ErrorTraceLevel::Minimal) ? "minimal" :
        (errorTraceLevel == ErrorTraceLevel::Basic) ? "basic" :
        (errorTraceLevel == ErrorTraceLevel::Extended) ? "extended" :
        ""
    };
    return _str;
}

// #################################################################################################

namespace {
    namespace local {
        static struct luaL_Reg const sLaneFunctions[] = {
            { "__gc", lane_gc },
            { "__index", LG_thread_index },
            { "cancel", LG_thread_cancel },
            { "get_debug_threadname", LG_get_debug_threadname },
            { "join", LG_thread_join },
            { nullptr, nullptr }
        };
    } // namespace local
} // namespace

  // contains keys: { __gc, __index, cached_error, cached_tostring, cancel, join, get_debug_threadname }
void Lane::PushMetatable(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 0);
    if (luaL_newmetatable(L_, kLaneMetatableName)) {                                               // L_: mt
        luaG_registerlibfuncs(L_, local::sLaneFunctions);
        // cache error() and tostring()
        kCachedError.pushKey(L_);                                                                  // L_: mt kCachedError
        lua_getglobal(L_, "error");                                                                // L_: mt kCachedError error()
        lua_rawset(L_, -3);                                                                        // L_: mt
        kCachedTostring.pushKey(L_);                                                               // L_: mt kCachedTostring
        lua_getglobal(L_, "tostring");                                                             // L_: mt kCachedTostring tostring()
        lua_rawset(L_, -3);                                                                        // L_: mt
        // hide the actual metatable from getmetatable()
        lua_pushliteral(L_, kLaneMetatableName);                                                   // L_: mt "Lane"
        lua_setfield(L_, -2, "__metatable");                                                       // L_: mt
    }
    STACK_CHECK(L_, 1);
}

// #################################################################################################

[[nodiscard]] int Lane::pushErrorHandler() const
{
    if (errorTraceLevel != ErrorTraceLevel::Minimal) {
        lua_pushcfunction(L, lane_error);
        return 1;
    }
    return 0;
}

// #################################################################################################

[[nodiscard]] std::string_view Lane::pushThreadStatus(lua_State* L_) const
{
    std::string_view const _str{ threadStatusString() };
    LUA_ASSERT(L_, !_str.empty());

    return lua_pushstringview(L_, _str);
}

// #################################################################################################

[[nodiscard]] std::string_view Lane::pushErrorTraceLevel(lua_State* L_) const
{
    std::string_view const _str{ errorTraceLevelString() };
    LUA_ASSERT(L_, !_str.empty());

    return lua_pushstringview(L_, _str);
}

// #################################################################################################

// intern the debug name in the caller lua state so that the pointer remains valid after the lane's state is closed
void Lane::securizeDebugName(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 0);
    STACK_GROW(L_, 3);
    // a Lane's uservalue should be a table
    lua_getiuservalue(L_, 1, 1);                                                                   // L_: lane ... {uv}
    LUA_ASSERT(L_, lua_istable(L_, -1));
    // we don't care about the actual key, so long as it's unique and can't collide with anything.
    lua_newtable(L_);                                                                              // L_: lane ... {uv} {}
    debugName = lua_pushstringview(L_, debugName);                                                 // L_: lane ... {uv} {} name
    lua_rawset(L_, -3);                                                                            // L_: lane ... {uv}
    lua_pop(L_, 1);                                                                                // L_: lane
    STACK_CHECK(L_, 0);
}

// #################################################################################################

void Lane::startThread(int priority_)
{
    thread = std::jthread([this]() { lane_main(this); });
    if (priority_ != kThreadPrioDefault) {
        JTHREAD_SET_PRIORITY(thread, priority_, U->sudo);
    }
}

// #################################################################################################

//---
// str= thread_status( lane )
//
// Returns: "pending"   not started yet
//          -> "running"   started, doing its work..
//             <-> "waiting"   blocked in a receive()
//                -> "done"     finished, results are there
//                   / "error"     finished at an error, error value is there
//                   / "cancelled"   execution cancelled by M (state gone)
//
[[nodiscard]] std::string_view Lane::threadStatusString() const
{
    std::string_view const _str{
        (status == Lane::Pending) ? "pending" :
        (status == Lane::Running) ? "running" :    // like in 'co.status()'
        (status == Lane::Waiting) ? "waiting" :
        (status == Lane::Done) ? "done" :
        (status == Lane::Error) ? "error" :
        (status == Lane::Cancelled) ? "cancelled" :
        ""
    };
    return _str;
}

// #################################################################################################

bool Lane::waitForCompletion(std::chrono::time_point<std::chrono::steady_clock> until_)
{
    std::unique_lock _guard{ doneMutex };
    // std::stop_token token{ thread.get_stop_token() };
    // return doneCondVar.wait_until(lock, token, secs_, [this](){ return status >= Lane::Done; });
    return doneCondVar.wait_until(_guard, until_, [this]() { return status >= Lane::Done; });
}
