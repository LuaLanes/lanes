/*
===============================================================================

Copyright (C) 2024 benoit Germain <bnt.germain@gmail.com>

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
#include "_pch.h"
#include "intercopycontext.h"

#include "debugspew.h"
#include "deep.h"
#include "keeper.h"
#include "lane.h"
#include "linda.h"
#include "nameof.h"
#include "universe.h"

// #################################################################################################

// Lua 5.4.3 style of dumping (see lstrlib.c)
// we have to do it that way because we can't unbalance the stack between buffer operations
// namely, this means we can't push a function on top of the stack *after* we initialize the buffer!
// luckily, this also works with earlier Lua versions
[[nodiscard]] static int buf_writer(lua_State* L_, void const* b_, size_t size_, void* ud_)
{
    luaL_Buffer* const _B{ static_cast<luaL_Buffer*>(ud_) };
    if (!_B->L) {
        luaL_buffinit(L_, _B);
    }
    luaL_addlstring(_B, static_cast<char const*>(b_), size_);
    return 0;
}

// #################################################################################################

// function sentinel used to transfer native functions from/to keeper states
[[nodiscard]] static int func_lookup_sentinel(lua_State* L_)
{
    raise_luaL_error(L_, "function lookup sentinel for %s, should never be called", lua_tostring(L_, lua_upvalueindex(1)));
}

// #################################################################################################

// function sentinel used to transfer native table from/to keeper states
[[nodiscard]] static int table_lookup_sentinel(lua_State* L_)
{
    raise_luaL_error(L_, "table lookup sentinel for %s, should never be called", lua_tostring(L_, lua_upvalueindex(1)));
}

// #################################################################################################

// function sentinel used to transfer cloned full userdata from/to keeper states
[[nodiscard]] static int userdata_clone_sentinel(lua_State* L_)
{
    raise_luaL_error(L_, "userdata clone sentinel for %s, should never be called", lua_tostring(L_, lua_upvalueindex(1)));
}

// #################################################################################################

// retrieve the name of a function/table in the lookup database
[[nodiscard]] std::string_view InterCopyContext::findLookupName() const
{
    LUA_ASSERT(L1, lua_isfunction(L1, L1_i) || lua_istable(L1, L1_i));                             // L1: ... v ...
    STACK_CHECK_START_REL(L1, 0);
    STACK_GROW(L1, 3); // up to 3 slots are necessary on error
    if (mode == LookupMode::FromKeeper) {
        lua_CFunction const _f{ lua_tocfunction(L1, L1_i) }; // should *always* be one of the function sentinels
        if (_f == func_lookup_sentinel || _f == table_lookup_sentinel || _f == userdata_clone_sentinel) {
            lua_getupvalue(L1, L1_i, 1);                                                           // L1: ... v ... "f.q.n"
        } else {
            // if this is not a sentinel, this is some user-created table we wanted to lookup
            LUA_ASSERT(L1, nullptr == _f && lua_istable(L1, L1_i));
            // push anything that will convert to nullptr string
            lua_pushnil(L1);                                                                       // L1: ... v ... nil
        }
    } else {
        // fetch the name from the source state's lookup table
        kLookupRegKey.pushValue(L1);                                                               // L1: ... v ... {}
        STACK_CHECK(L1, 1);
        LUA_ASSERT(L1, lua_istable(L1, -1));
        lua_pushvalue(L1, L1_i);                                                                   // L1: ... v ... {} v
        lua_rawget(L1, -2);                                                                        // L1: ... v ... {} "f.q.n"
    }
    std::string_view _fqn{ luaG_tostring(L1, -1) };
    DEBUGSPEW_CODE(DebugSpew(U) << "function [C] " << _fqn << std::endl);
    // popping doesn't invalidate the pointer since this is an interned string gotten from the lookup database
    lua_pop(L1, (mode == LookupMode::FromKeeper) ? 1 : 2);                                         // L1: ... v ...
    STACK_CHECK(L1, 0);
    if (_fqn.empty() && !lua_istable(L1, L1_i)) { // raise an error if we try to send an unknown function (but not for tables)
        // try to discover the name of the function we want to send
        kLaneNameRegKey.pushValue(L1);                                                             // L1: ... v ... lane_name
        std::string_view const _from{ luaG_tostring(L1, -1) };
        lua_pushcfunction(L1, LG_nameof);                                                          // L1: ... v ... lane_name LG_nameof
        lua_pushvalue(L1, L1_i);                                                                   // L1: ... v ... lane_name LG_nameof t
        lua_call(L1, 1, 2);                                                                        // L1: ... v ... lane_name "type" "name"|nil
        std::string_view const _typewhat{ (luaG_type(L1, -2) == LuaType::STRING) ? luaG_tostring(L1, -2) : luaG_typename(L1, -2) };
        // second return value can be nil if the table was not found
        // probable reason: the function was removed from the source Lua state before Lanes was required.
        std::string_view _what, _gotchaA, _gotchaB;
        if (lua_isnil(L1, -1)) {
            _gotchaA = " referenced by";
            _gotchaB = "\n(did you remove it from the source Lua state before requiring Lanes?)";
            _what = name;
        } else {
            _gotchaA = "";
            _gotchaB = "";
            _what = (luaG_type(L1, -1) == LuaType::STRING) ? luaG_tostring(L1, -1) : luaG_typename(L1, -1);
        }
        raise_luaL_error(L1, "%s%s '%s' not found in %s origin transfer database.%s", _typewhat.data(), _gotchaA.data(), _what.data(), _from.empty() ? "main" : _from.data(), _gotchaB.data());
    }
    STACK_CHECK(L1, 0);
    return _fqn;
}

// #################################################################################################

/*---=== Inter-state copying ===---*/

// xxh64 of string "kMtIdRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kMtIdRegKey{ 0xA8895DCF4EC3FE3Cull };

// get a unique ID for metatable at [i].
[[nodiscard]] static lua_Integer get_mt_id(Universe* U_, lua_State* L_, int idx_)
{
    idx_ = luaG_absindex(L_, idx_);

    STACK_GROW(L_, 3);

    STACK_CHECK_START_REL(L_, 0);
    std::ignore = kMtIdRegKey.getSubTable(L_, 0, 0);                                               // L_: ... _R[kMtIdRegKey]
    lua_pushvalue(L_, idx_);                                                                       // L_: ... _R[kMtIdRegKey] {mt}
    lua_rawget(L_, -2);                                                                            // L_: ... _R[kMtIdRegKey] mtk?

    lua_Integer _id{ lua_tointeger(L_, -1) }; // 0 for nil
    lua_pop(L_, 1);                                                                                // L_: ... _R[kMtIdRegKey]
    STACK_CHECK(L_, 1);

    if (_id == 0) {
        _id = U_->nextMetatableId.fetch_add(1, std::memory_order_relaxed);

        // Create two-way references: id_uint <-> table
        lua_pushvalue(L_, idx_);                                                                   // L_: ... _R[kMtIdRegKey] {mt}
        lua_pushinteger(L_, _id);                                                                  // L_: ... _R[kMtIdRegKey] {mt} id
        lua_rawset(L_, -3);                                                                        // L_: ... _R[kMtIdRegKey]

        lua_pushinteger(L_, _id);                                                                  // L_: ... _R[kMtIdRegKey] id
        lua_pushvalue(L_, idx_);                                                                   // L_: ... _R[kMtIdRegKey] id {mt}
        lua_rawset(L_, -3);                                                                        // L_: ... _R[kMtIdRegKey]
    }
    lua_pop(L_, 1);                                                                                // L_: ...
    STACK_CHECK(L_, 0);

    return _id;
}

// #################################################################################################

// Copy a function over, which has not been found in the cache.
// L2 has the cache key for this function at the top of the stack
void InterCopyContext::copyFunction() const
{
    LUA_ASSERT(L1, L2_cache_i != 0);                                                               //                                                L2: ... {cache} ... p
    STACK_GROW(L1, 2);
    STACK_CHECK_START_REL(L1, 0);

    // 'luaG_dump()' needs the function at top of stack
    // if already on top of the stack, no need to push again
    bool const _needToPush{ L1_i != lua_gettop(L1) };
    if (_needToPush) {
        lua_pushvalue(L1, L1_i);                                                                   // L1: ... f
    }

    //
    // "value returned is the error code returned by the last call
    // to the writer" (and we only return 0)
    // not sure this could ever fail but for memory shortage reasons
    // last argument is Lua 5.4-specific (no stripping)
    luaL_Buffer B{};
    if (luaG_dump(L1, buf_writer, &B, U->stripFunctions) != 0) {
        raise_luaL_error(getErrL(), "internal error: function dump failed.");
    }

    // pushes dumped string on 'L1'
    luaL_pushresult(&B);                                                                           // L1: ... f b

    // if not pushed, no need to pop
    if (_needToPush) {
        lua_remove(L1, -2);                                                                        // L1: ... b
    }

    // transfer the bytecode, then the upvalues, to create a similar closure
    {
        char const* _fname{};
#define LOG_FUNC_INFO 0
        if constexpr (LOG_FUNC_INFO)
        {
            lua_Debug _ar;
            lua_pushvalue(L1, L1_i);                                                               // L1: ... b f
            // "To get information about a function you push it onto the stack and start the what string with the character '>'."
            // fills 'fname' 'namewhat' and 'linedefined', pops function
            lua_getinfo(L1, ">nS", &_ar);                                                          // L1: ... b
            _fname = _ar.namewhat;
            DEBUGSPEW_CODE(DebugSpew(U) << "FNAME: " << _ar.short_src << " @ " << _ar.linedefined << std::endl);
        }

        {
            std::string_view const _bytecode{ luaG_tostring(L1, -1) };                             // L1: ... b
            LUA_ASSERT(L1, !_bytecode.empty());
            STACK_GROW(L2, 2);
            // Note: Line numbers seem to be taken precisely from the
            //       original function. 'fname' is not used since the chunk
            //       is precompiled (it seems...).
            //
            // TBD: Can we get the function's original name through, as well?
            //
            if (luaL_loadbuffer(L2, _bytecode.data(), _bytecode.size(), _fname) != 0) {            //                                                L2: ... {cache} ... p function
                // chunk is precompiled so only LUA_ERRMEM can happen
                // "Otherwise, it pushes an error message"
                //
                STACK_GROW(L1, 1);
                raise_luaL_error(getErrL(), "%s: %s", _fname, lua_tostring(L2, -1));
            }
            // remove the dumped string
            lua_pop(L1, 1); // ...
            // now set the cache as soon as we can.
            // this is necessary if one of the function's upvalues references it indirectly
            // we need to find it in the cache even if it isn't fully transfered yet
            lua_insert(L2, -2);                                                                    //                                                L2: ... {cache} ... function p
            lua_pushvalue(L2, -2);                                                                 //                                                L2: ... {cache} ... function p function
            // cache[p] = function
            lua_rawset(L2, L2_cache_i);                                                            //                                                L2: ... {cache} ... function
        }
        STACK_CHECK(L1, 0);

        /* push over any upvalues; references to this function will come from
         * cache so we don't end up in eternal loop.
         * Lua5.2 and Lua5.3: one of the upvalues is _ENV, which we don't want to copy!
         * instead, the function shall have LUA_RIDX_GLOBALS taken in the destination state!
         * TODO: this can probably be factorized as InterCopyContext::copyUpvalues(...)
         */
        int _n{ 0 };
        {
            InterCopyContext _c{ U, L2, L1, L2_cache_i, {}, VT::NORMAL, mode, {} };
            // if we encounter an upvalue equal to the global table in the source, bind it to the destination's global table
            luaG_pushglobaltable(L1);                                                              // L1: ... _G
            for (char const* _upname{}; (_upname = lua_getupvalue(L1, L1_i, 1 + _n)); ++_n) {      // L1: ... _G up[n]
                DEBUGSPEW_CODE(DebugSpew(U) << "UPNAME[" << _n << "]: " << _c.name << " -> ");
                if (lua_rawequal(L1, -1, -2)) { // is the upvalue equal to the global table?
                    DEBUGSPEW_CODE(DebugSpew(nullptr) << "pushing destination global scope" << std::endl);
                    luaG_pushglobaltable(L2);                                                      //                                                L2: ... {cache} ... function <upvalues>
                } else {
                    DEBUGSPEW_CODE(DebugSpew(nullptr) << "copying value" << std::endl);
                    _c.name = _upname;
                    _c.L1_i = SourceIndex{ lua_gettop(L1) };
                    if (_c.interCopyOne() != InterCopyResult::Success) {                           //                                                L2: ... {cache} ... function <upvalues>
                        raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
                    }
                }
                lua_pop(L1, 1);                                                                    // L1: ... _G
            }
            lua_pop(L1, 1);                                                                        // L1: ...
        }                                                                                          //                                                L2: ... {cache} ... function + 'n' upvalues (>=0)
        STACK_CHECK(L1, 0);

        // Set upvalues (originally set to 'nil' by 'lua_load')
        for (int const _func_index{ lua_gettop(L2) - _n }; _n > 0; --_n) {
            // assign upvalue, popping it from the stack
            [[maybe_unused]] std::string_view const _upname{ lua_setupvalue(L2, _func_index, _n) };//                                                L2: ... {cache} ... function
            LUA_ASSERT(L1, !_upname.empty()); // not having enough slots?
        }
        // once all upvalues have been set we are left
        // with the function at the top of the stack                                               //                                                L2: ... {cache} ... function
    }
    STACK_CHECK(L1, 0);
}

// #################################################################################################

// Push a looked-up native/LuaJIT function.
void InterCopyContext::lookupNativeFunction() const
{
    // get the name of the function we want to send
    std::string_view const _fqn{ findLookupName() };
    // push the equivalent function in the destination's stack, retrieved from the lookup table
    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 3); // up to 3 slots are necessary on error
    switch (mode) {
    default: // shouldn't happen, in theory...
        raise_luaL_error(getErrL(), "internal error: unknown lookup mode");
        break;

    case LookupMode::ToKeeper:
        // push a sentinel closure that holds the lookup name as upvalue
        luaG_pushstring(L2, _fqn);                                                                 // L1: ... f ...                                  L2: "f.q.n"
        lua_pushcclosure(L2, func_lookup_sentinel, 1);                                             // L1: ... f ...                                  L2: f
        break;

    case LookupMode::LaneBody:
    case LookupMode::FromKeeper:
        kLookupRegKey.pushValue(L2);                                                               // L1: ... f ...                                  L2: {}
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_istable(L2, -1));
        luaG_pushstring(L2, _fqn);                                                                 // L1: ... f ...                                  L2: {} "f.q.n"
        lua_rawget(L2, -2);                                                                        // L1: ... f ...                                  L2: {} f
        // nil means we don't know how to transfer stuff: user should do something
        // anything other than function or table should not happen!
        if (!lua_isfunction(L2, -1) && !lua_istable(L2, -1)) {
            kLaneNameRegKey.pushValue(L1);                                                         // L1: ... f ... lane_name
            std::string_view const _from{ luaG_tostring(L1, -1) };
            lua_pop(L1, 1);                                                                        // L1: ... f ...
            kLaneNameRegKey.pushValue(L2);                                                         // L1: ... f ...                                  L2: {} f lane_name
            std::string_view const _to{ luaG_tostring(L2, -1) };
            lua_pop(L2, 1);                                                                        //                                                L2: {} f
            raise_luaL_error(
                getErrL(),
                "%s%s: function '%s' not found in %s destination transfer database.",
                lua_isnil(L2, -1) ? "" : "INTERNAL ERROR IN ",
                _from.empty() ? "main" : _from.data(),
                _fqn.data(),
                _to.empty() ? "main" : _to.data());
            return;
        }
        lua_remove(L2, -2);                                                                        // L2: f
        break;
    }
    STACK_CHECK(L2, 1);
}

// #################################################################################################

// Check if we've already copied the same function from 'L1', and reuse the old copy.
// Always pushes a function to 'L2'.
void InterCopyContext::copyCachedFunction() const
{
    FuncSubType const _funcSubType{ luaG_getfuncsubtype(L1, L1_i) };
    if (_funcSubType == FuncSubType::Bytecode) {
        void* const _aspointer{ const_cast<void*>(lua_topointer(L1, L1_i)) };
        // TODO: Merge this and same code for tables
        LUA_ASSERT(L1, L2_cache_i != 0);

        STACK_GROW(L2, 2);

        // L2_cache[id_str]= function
        //
        STACK_CHECK_START_REL(L2, 0);

        // We don't need to use the from state ('L1') in ID since the life span
        // is only for the duration of a copy (both states are locked).

        // push a light userdata uniquely representing the function
        lua_pushlightuserdata(L2, _aspointer);                                                     //                                                L2: ... {cache} ... p

        //DEBUGSPEW_CODE(DebugSpew(U) << "<< ID: " << luaG_tostring(L2, -1) << " >>" << std::endl);

        lua_pushvalue(L2, -1);                                                                     //                                                L2: ... {cache} ... p p
        lua_rawget(L2, L2_cache_i);                                                                //                                                L2: ... {cache} ... p function|nil|true

        if (lua_isnil(L2, -1)) { // function is unknown
            lua_pop(L2, 1);                                                                        //                                                L2: ... {cache} ... p

            // Set to 'true' for the duration of creation; need to find self-references
            // via upvalues
            //
            // pushes a copy of the func, stores a reference in the cache
            copyFunction();                                                                        //                                                L2: ... {cache} ... function
        } else { // found function in the cache
            lua_remove(L2, -2);                                                                    //                                                L2: ... {cache} ... function
        }
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_isfunction(L2, -1));
    } else { // function is native/LuaJIT: no need to cache
        lookupNativeFunction();                                                                    //                                                L2: ... {cache} ... function
        // if the function was in fact a lookup sentinel, we can either get a function or a table here
        LUA_ASSERT(L1, lua_isfunction(L2, -1) || lua_istable(L2, -1));
    }
}

// #################################################################################################

// Push a looked-up table, or nothing if we found nothing
[[nodiscard]] bool InterCopyContext::lookupTable() const
{
    // get the name of the table we want to send
    std::string_view const _fqn{ findLookupName() };
    if (_fqn.empty()) { // name not found, it is some user-created table
        return false;
    }
    // push the equivalent table in the destination's stack, retrieved from the lookup table
    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 3); // up to 3 slots are necessary on error
    switch (mode) {
    default: // shouldn't happen, in theory...
        raise_luaL_error(getErrL(), "internal error: unknown lookup mode");
        break;

    case LookupMode::ToKeeper:
        // push a sentinel closure that holds the lookup name as upvalue
        luaG_pushstring(L2, _fqn);                                                                 // L1: ... t ...                                  L2: "f.q.n"
        lua_pushcclosure(L2, table_lookup_sentinel, 1);                                            // L1: ... t ...                                  L2: f
        break;

    case LookupMode::LaneBody:
    case LookupMode::FromKeeper:
        kLookupRegKey.pushValue(L2);                                                               // L1: ... t ...                                  L2: {}
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_istable(L2, -1));
        luaG_pushstring(L2, _fqn);                                                                 //                                                L2: {} "f.q.n"
        lua_rawget(L2, -2);                                                                        //                                                L2: {} t
        // we accept destination lookup failures in the case of transfering the Lanes body function (this will result in the source table being cloned instead)
        // but not when we extract something out of a keeper, as there is nothing to clone!
        if (lua_isnil(L2, -1) && mode == LookupMode::LaneBody) {
            lua_pop(L2, 2);                                                                        // L1: ... t ...                                  L2:
            STACK_CHECK(L2, 0);
            return false;
        } else if (!lua_istable(L2, -1)) { // this can happen if someone decides to replace same already registered item (for a example a standard lib function) with a table
            kLaneNameRegKey.pushValue(L1);                                                         // L1: ... t ... lane_name
            std::string_view const _from{ luaG_tostring(L1, -1) };
            lua_pop(L1, 1);                                                                        // L1: ... t ...
            kLaneNameRegKey.pushValue(L2);                                                         // L1: ... t ...                                  L2: {} t lane_name
            std::string_view const _to{ luaG_tostring(L2, -1) };
            lua_pop(L2, 1);                                                                        // L1: ... t ...                                  L2: {} t
            raise_luaL_error(
                getErrL(),
                "%s: source table '%s' found as %s in %s destination transfer database.",
                _from.empty() ? "main" : _from.data(),
                _fqn.data(),
                luaG_typename(L2, -1).data(),
                _to.empty() ? "main" : _to.data());
        }
        lua_remove(L2, -2);                                                                        // L1: ... t ...                                  L2: t
        break;
    }
    STACK_CHECK(L2, 1);
    return true;
}

// #################################################################################################

void InterCopyContext::interCopyKeyValuePair() const
{
    SourceIndex const _val_i{ lua_gettop(L1) };
    SourceIndex const _key_i{ _val_i - 1 };

    // For the key, only basic key types are copied over. others ignored
    InterCopyContext _c{ U, L2, L1, L2_cache_i, _key_i, VT::KEY, mode, name };
    if (_c.interCopyOne() != InterCopyResult::Success) {
        return;
        // we could raise an error instead of ignoring the table entry, like so:
        // raise_luaL_error(L1, "Unable to copy %s key '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", name, luaL_typename(L1, key_i));
        // maybe offer this possibility as a global configuration option, or a linda setting, or as a argument of the call causing the transfer?
    }

    char* _valPath{ nullptr };
    if (U->verboseErrors) {
        // for debug purposes, let's try to build a useful name
        if (luaG_type(L1, _key_i) == LuaType::STRING) {
            std::string_view const _key{ luaG_tostring(L1, _key_i) };
            size_t const _bufLen{ name.size() + _key.size() + 2 }; // +2 for separator dot and terminating 0
            _valPath = static_cast<char*>(alloca(_bufLen));
            sprintf(_valPath, "%s." STRINGVIEW_FMT, name.data(), (int) _key.size(), _key.data());
        }
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (lua_isinteger(L1, _key_i)) {
            lua_Integer const key{ lua_tointeger(L1, _key_i) };
            _valPath = (char*) alloca(name.size() + 32 + 3); // +3 for [] and terminating 0
            sprintf(_valPath, "%s[" LUA_INTEGER_FMT "]", name.data(), key);
        }
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (luaG_type(L1, _key_i) == LuaType::NUMBER) {
            lua_Number const key{ lua_tonumber(L1, _key_i) };
            _valPath = (char*) alloca(name.size() + 32 + 3); // +3 for [] and terminating 0
            sprintf(_valPath, "%s[" LUA_NUMBER_FMT "]", name.data(), key);
        } else if (luaG_type(L1, _key_i) == LuaType::LIGHTUSERDATA) {
            void* const key{ lua_touserdata(L1, _key_i) };
            _valPath = (char*) alloca(name.size() + 16 + 5); // +5 for [U:] and terminating 0
            sprintf(_valPath, "%s[U:%p]", name.data(), key);
        } else if (luaG_type(L1, _key_i) == LuaType::BOOLEAN) {
            int const key{ lua_toboolean(L1, _key_i) };
            _valPath = (char*) alloca(name.size() + 8); // +8 for [], 'false' and terminating 0
            sprintf(_valPath, "%s[%s]", name.data(), key ? "true" : "false");
        }
    }

    _c.L1_i = SourceIndex{ _val_i };
    // Contents of metatables are copied with cache checking. important to detect loops.
    _c.vt = VT::NORMAL;
    _c.name = _valPath ? _valPath : name;
    if (_c.interCopyOne() != InterCopyResult::Success) {
        raise_luaL_error(getErrL(), "Unable to copy %s entry '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", _valPath, luaL_typename(L1, _val_i));
    }
    LUA_ASSERT(L1, lua_istable(L2, -3));
    lua_rawset(L2, -3); // add to table (pops key & val)
}

// #################################################################################################

LuaType InterCopyContext::processConversion() const
{
    static constexpr int kPODmask = (1 << LUA_TNIL) | (1 << LUA_TBOOLEAN) | (1 << LUA_TLIGHTUSERDATA) | (1 << LUA_TNUMBER) | (1 << LUA_TSTRING);

    LuaType _val_type{ luaG_type(L1, L1_i) };

    STACK_CHECK_START_REL(L1, 0);

    // it's a POD: nothing to do
    if (((1 << static_cast<int>(_val_type)) & kPODmask) != 0) {
        return _val_type;
    }

    // no metatable: nothing to do
    if (!lua_getmetatable(L1, L1_i)) {                                                             // L1: ...
        STACK_CHECK(L1, 0);
        return _val_type;
    }
    // we have a metatable                                                                         // L1: ... mt
    static constexpr std::string_view kConvertField{ "__lanesconvert" };
    LuaType const _converterType{ luaG_getfield(L1, -1, kConvertField) };                          // L1: ... mt kConvertField
    switch (_converterType) {
    case LuaType::NIL:
        // no __lanesconvert, nothing to do
        lua_pop(L1, 2);                                                                            // L1: ...
        break;

    case LuaType::LIGHTUSERDATA:
        if (kNilSentinel.equals(L1, -1)) {
            DEBUGSPEW_CODE(DebugSpew(U) << "converted " << luaG_typename(L1, _val_type) << " to nil" << std::endl);
            lua_replace(L1, L1_i);                                                                 // L1: ... mt
            lua_pop(L1, 1);                                                                        // L1: ...
            _val_type = _converterType;
        } else {
            raise_luaL_error(getErrL(), "Invalid %s type %s", kConvertField.data(), luaG_typename(L1, _converterType).data());
        }
        break;

    case LuaType::STRING:
        // kConvertField == "decay" -> replace source value with it's pointer
        if (std::string_view const _mode{ luaG_tostring(L1, -1) }; _mode == "decay") {
            lua_pop(L1, 1);                                                                        // L1: ... mt
            lua_pushlightuserdata(L1, const_cast<void*>(lua_topointer(L1, L1_i)));                 // L1: ... mt decayed
            lua_replace(L1, L1_i);                                                                 // L1: ... mt
            lua_pop(L1, 1);                                                                        // L1: ...
            _val_type = LuaType::LIGHTUSERDATA;
        } else {
            raise_luaL_error(getErrL(), "Invalid %s mode '%s'", kConvertField.data(), _mode.data());
        }
        break;

    case LuaType::FUNCTION:
        lua_pushvalue(L1, L1_i);                                                                   // L1: ... mt kConvertField val
        luaG_pushstring(L1, mode == LookupMode::ToKeeper ? "keeper" : "regular");                  // L1: ... mt kConvertField val string
        lua_call(L1, 2, 1); // val:kConvertField(str) -> result                                    // L1: ... mt kConvertField converted
        lua_replace(L1, L1_i);                                                                     // L1: ... mt
        lua_pop(L1, 1);                                                                            // L1: ... mt
        _val_type =  luaG_type(L1, L1_i);
        break;

    default:
        raise_luaL_error(getErrL(), "Invalid %s type %s", kConvertField.data(), luaG_typename(L1, _converterType).data());
    }
    STACK_CHECK(L1, 0);
    LUA_ASSERT(getErrL(), luaG_type(L1, L1_i) == _val_type);
    return _val_type;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::pushCachedMetatable() const
{
    STACK_CHECK_START_REL(L1, 0);
    if (!lua_getmetatable(L1, L1_i)) {                                                             // L1: ... mt
        STACK_CHECK(L1, 0);
        return false;
    }
    STACK_CHECK(L1, 1);

    lua_Integer const _mt_id{ get_mt_id(U, L1, -1) }; // Unique id for the metatable

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 4);
    // do we already know this metatable?
    std::ignore = kMtIdRegKey.getSubTable(L2, 0, 0);                                               //                                                L2: _R[kMtIdRegKey]
    lua_pushinteger(L2, _mt_id);                                                                   //                                                L2: _R[kMtIdRegKey] id
    lua_rawget(L2, -2);                                                                            //                                                L2: _R[kMtIdRegKey] mt|nil
    STACK_CHECK(L2, 2);

    if (lua_isnil(L2, -1)) { // L2 did not know the metatable
        lua_pop(L2, 1);                                                                            //                                                L2: _R[kMtIdRegKey]
        InterCopyContext const _c{ U, L2, L1, L2_cache_i, SourceIndex{ lua_gettop(L1) }, VT::METATABLE, mode, name };
        if (_c.interCopyOne() != InterCopyResult::Success) {                                       //                                                L2: _R[kMtIdRegKey] mt?
            raise_luaL_error(getErrL(), "Error copying a metatable");
        }

        STACK_CHECK(L2, 2);                                                                        //                                                L2: _R[kMtIdRegKey] mt
        // mt_id -> metatable
        lua_pushinteger(L2, _mt_id);                                                               //                                                L2: _R[kMtIdRegKey] mt id
        lua_pushvalue(L2, -2);                                                                     //                                                L2: _R[kMtIdRegKey] mt id mt
        lua_rawset(L2, -4);                                                                        //                                                L2: _R[kMtIdRegKey] mt

        // metatable -> mt_id
        lua_pushvalue(L2, -1);                                                                     //                                                L2: _R[kMtIdRegKey] mt mt
        lua_pushinteger(L2, _mt_id);                                                               //                                                L2: _R[kMtIdRegKey] mt mt id
        lua_rawset(L2, -4);                                                                        //                                                L2: _R[kMtIdRegKey] mt
        STACK_CHECK(L2, 2);
    }
    lua_remove(L2, -2);                                                                            //                                                L2: mt

    lua_pop(L1, 1);                                                                                // L1: ...
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

// Check if we've already copied the same table from 'L1', and reuse the old copy. This allows table upvalues shared by multiple
// local functions to point to the same table, also in the target.
// Always pushes a table to 'L2'.
// Returns true if the table was cached (no need to fill it!); false if it's a virgin.
[[nodiscard]] bool InterCopyContext::pushCachedTable() const
{
    void const* const _p{ lua_topointer(L1, L1_i) };

    LUA_ASSERT(L1, L2_cache_i != 0);
    STACK_GROW(L2, 3);
    STACK_CHECK_START_REL(L2, 0);

    // We don't need to use the from state ('L1') in ID since the life span
    // is only for the duration of a copy (both states are locked).
    // push a light userdata uniquely representing the table
    lua_pushlightuserdata(L2, const_cast<void*>(_p));                                              // L1: ... t ...                                  L2: ... p

    //DEBUGSPEW_CODE(DebugSpew(U) << "<< ID: " << luaG_tostring(L2, -1) << " >>" << std::endl);

    lua_rawget(L2, L2_cache_i);                                                                    // L1: ... t ...                                  L2: ... {cached|nil}
    bool const _not_found_in_cache{ lua_isnil(L2, -1) };
    if (_not_found_in_cache) {
        // create a new entry in the cache
        lua_pop(L2, 1);                                                                            // L1: ... t ...                                  L2: ...
        lua_newtable(L2);                                                                          // L1: ... t ...                                  L2: ... {}
        lua_pushlightuserdata(L2, const_cast<void*>(_p));                                          // L1: ... t ...                                  L2: ... {} p
        lua_pushvalue(L2, -2);                                                                     // L1: ... t ...                                  L2: ... {} p {}
        lua_rawset(L2, L2_cache_i);                                                                // L1: ... t ...                                  L2: ... {}
    }
    STACK_CHECK(L2, 1);
    LUA_ASSERT(L1, lua_istable(L2, -1));
    return !_not_found_in_cache;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::tryCopyClonable() const
{
    SourceIndex const _L1_i{ luaG_absindex(L1, L1_i) };
    void* const _source{ lua_touserdata(L1, _L1_i) };

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    // Check if the source was already cloned during this copy
    lua_pushlightuserdata(L2, _source);                                                            //                                                L2: ... source
    lua_rawget(L2, L2_cache_i);                                                                    //                                                L2: ... clone?
    if (!lua_isnil(L2, -1)) {
        STACK_CHECK(L2, 1);
        return true;
    } else {
        lua_pop(L2, 1);                                                                            //                                                L2: ...
    }
    STACK_CHECK(L2, 0);

    // no metatable? -> not clonable
    if (!lua_getmetatable(L1, _L1_i)) {                                                            // L1: ... mt?
        STACK_CHECK(L1, 0);
        return false;
    }

    // no __lanesclone? -> not clonable
    if (luaG_getfield(L1, -1, "__lanesclone") == LuaType::NIL) {                                   // L1: ... mt nil
        lua_pop(L1, 2);                                                                            // L1: ...
        STACK_CHECK(L1, 0);
        return false;
    }

    DEBUGSPEW_CODE(DebugSpew(U) << "CLONABLE USERDATA" << std::endl);

    // we need to copy over the uservalues of the userdata as well
    {
        int const _mt{ luaG_absindex(L1, -2) };                                                    // L1: ... mt __lanesclone
        size_t const userdata_size{ lua_rawlen(L1, _L1_i) };
        // extract all the uservalues, but don't transfer them yet
        int const _nuv{ luaG_getalluservalues(L1, _L1_i) };                                        // L1: ... mt __lanesclone [uv]*
        // create the clone userdata with the required number of uservalue slots
        void* const _clone{ lua_newuserdatauv(L2, userdata_size, _nuv) };                          //                                                L2: ... u
        // copy the metatable in the target state, and give it to the clone we put there
        InterCopyContext _c{ U, L2, L1, L2_cache_i, SourceIndex{ _mt }, VT::NORMAL, mode, name };
        if (_c.interCopyOne() != InterCopyResult::Success) {                                       //                                                L2: ... u mt|sentinel
            raise_luaL_error(getErrL(), "Error copying a metatable");
        }

        if (LookupMode::ToKeeper == mode) {                                                        //                                                L2: ... u sentinel
            LUA_ASSERT(L1, lua_tocfunction(L2, -1) == table_lookup_sentinel);
            // we want to create a new closure with a 'clone sentinel' function, where the upvalues are the userdata and the metatable fqn
            lua_getupvalue(L2, -1, 1);                                                             //                                                L2: ... u sentinel fqn
            lua_remove(L2, -2);                                                                    //                                                L2: ... u fqn
            lua_insert(L2, -2);                                                                    //                                                L2: ... fqn u
            lua_pushcclosure(L2, userdata_clone_sentinel, 2);                                      //                                                L2: ... userdata_clone_sentinel
        } else { // from keeper or direct                                                          //                                                L2: ... u mt
            LUA_ASSERT(L1, lua_istable(L2, -1));
            lua_setmetatable(L2, -2);                                                              //                                                L2: ... u
        }
        STACK_CHECK(L2, 1);
        // first, add the entry in the cache (at this point it is either the actual userdata or the keeper sentinel
        lua_pushlightuserdata(L2, _source);                                                        //                                                L2: ... u source
        lua_pushvalue(L2, -2);                                                                     //                                                L2: ... u source u
        lua_rawset(L2, L2_cache_i);                                                                //                                                L2: ... u
        // make sure we have the userdata now
        if (LookupMode::ToKeeper == mode) {                                                        //                                                L2: ... userdata_clone_sentinel
            lua_getupvalue(L2, -1, 2);                                                             //                                                L2: ... userdata_clone_sentinel u
        }
        // assign uservalues
        int _uvi{ _nuv };
        while (_uvi > 0) {
            _c.L1_i = SourceIndex{ luaG_absindex(L1, -1) };
            if (_c.interCopyOne() != InterCopyResult::Success) {                                   //                                                L2: ... u uv
                raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
            }
            lua_pop(L1, 1);                                                                        // L1: ... mt __lanesclone [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, -2, _uvi);                                                       //                                                L2: ... u
            --_uvi;
        }
        // when we are done, all uservalues are popped from the source stack, and we want only the single transferred value in the destination
        if (LookupMode::ToKeeper == mode) {                                                        //                                                L2: ... userdata_clone_sentinel u
            lua_pop(L2, 1);                                                                        //                                                L2: ... userdata_clone_sentinel
        }
        STACK_CHECK(L2, 1);
        STACK_CHECK(L1, 2);
        // call cloning function in source state to perform the actual memory cloning
        lua_pushlightuserdata(L1, _clone);                                                         // L1: ... mt __lanesclone clone
        lua_pushlightuserdata(L1, _source);                                                        // L1: ... mt __lanesclone clone source
        lua_pushinteger(L1, static_cast<lua_Integer>(userdata_size));                              // L1: ... mt __lanesclone clone source size
        lua_call(L1, 3, 0);                                                                        // L1: ... mt
        STACK_CHECK(L1, 1);
    }

    STACK_CHECK(L2, 1);
    lua_pop(L1, 1);                                                                                // L1: ...
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

// Copy deep userdata between two separate Lua states (from L1 to L2)
// Returns false if not a deep userdata, else true (unless an error occured)
[[nodiscard]] bool InterCopyContext::tryCopyDeep() const
{
    DeepFactory* const _factory{ DeepFactory::LookupFactory(L1, L1_i, mode) };                     // L1: ... deep ...
    if (_factory == nullptr) {
        return false; // not a deep userdata
    }

    DEBUGSPEW_CODE(DebugSpew(U) << "DEEP USERDATA" << std::endl);
    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    // extract all uservalues of the source. unfortunately, the only way to know their count is to iterate until we fail
    int const _nuv{ luaG_getalluservalues(L1, L1_i) };                                             // L1: ... deep ... [uv]*
    STACK_CHECK(L1, _nuv);

    DeepPrelude* const _deep{ *luaG_tofulluserdata<DeepPrelude*>(L1, L1_i) };
    DeepFactory::PushDeepProxy(L2, _deep, _nuv, mode, getErrL());                                  // L1: ... deep ... [uv]*                           L2: deep

    // transfer all uservalues of the source in the destination
    {
        InterCopyContext _c{ U, L2, L1, L2_cache_i, {}, VT::NORMAL, mode, name };
        int const _clone_i{ lua_gettop(L2) };
        STACK_GROW(L2, _nuv);
        int _uvi{ _nuv };
        while (_uvi) {
            _c.L1_i = SourceIndex{ luaG_absindex(L1, -1) };
            if (_c.interCopyOne() != InterCopyResult::Success) {                                   // L1: ... deep ... [uv]*                           L2: deep uv
                raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
            }
            lua_pop(L1, 1);                                                                        // L1: ... deep ... [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, _clone_i, _uvi);                                                 //                                                  L2: deep
            --_uvi;
        } // loop done: no uv remains on L1 stack                                                  // L1: ... deep ...
    }

    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);

    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyBoolean() const
{
    int const _v{ lua_toboolean(L1, L1_i) };
    DEBUGSPEW_CODE(DebugSpew(nullptr) << (_v ? "true" : "false") << std::endl);
    lua_pushboolean(L2, _v);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyFunction() const
{
    if (vt == VT::KEY) {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(DebugSpew(nullptr) << "FUNCTION " << name << std::endl);

    if (lua_tocfunction(L1, L1_i) == userdata_clone_sentinel) { // we are actually copying a clonable full userdata from a keeper
        // clone the full userdata again

        // let's see if we already restored this userdata
        lua_getupvalue(L1, L1_i, 2);                                                               // L1: ... u
        void* _source{ lua_touserdata(L1, -1) };
        lua_pushlightuserdata(L2, _source);                                                        //                                                L2: ... source
        lua_rawget(L2, L2_cache_i);                                                                //                                                L2: ... u?
        if (!lua_isnil(L2, -1)) {
            lua_pop(L1, 1);                                                                        // L1: ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 1);
            return true;
        }
        lua_pop(L2, 1);                                                                            //                                                L2: ...

        // userdata_clone_sentinel has 2 upvalues: the fqn of its metatable, and the userdata itself
        bool const _found{ lookupTable() };                                                        //                                                L2: ... mt?
        if (!_found) {
            STACK_CHECK(L2, 0);
            return false;
        }
        // 'L1_i' slot was the proxy closure, but from now on we operate onthe actual userdata we extracted from it
        SourceIndex const source_i{ lua_gettop(L1) };
        _source = lua_touserdata(L1, -1);
        void* _clone{ nullptr };
        // get the number of bytes to allocate for the clone
        size_t const userdata_size{ lua_rawlen(L1, -1) };
        {
            // extract uservalues (don't transfer them yet)
            int const _nuv{ luaG_getalluservalues(L1, source_i) };                                 // L1: ... u [uv]*
            STACK_CHECK(L1, _nuv + 1);
            // create the clone userdata with the required number of uservalue slots
            _clone = lua_newuserdatauv(L2, userdata_size, _nuv);                                   //                                                L2: ... mt u
            // add it in the cache
            lua_pushlightuserdata(L2, _source);                                                    //                                                L2: ... mt u source
            lua_pushvalue(L2, -2);                                                                 //                                                L2: ... mt u source u
            lua_rawset(L2, L2_cache_i);                                                            //                                                L2: ... mt u
            // set metatable
            lua_pushvalue(L2, -2);                                                                 //                                                L2: ... mt u mt
            lua_setmetatable(L2, -2);                                                              //                                                L2: ... mt u
            // transfer and assign uservalues
            InterCopyContext _c{ *this };
            int _uvi{ _nuv };
            while (_uvi > 0) {
                _c.L1_i = SourceIndex{ luaG_absindex(L1, -1) };
                if (_c.interCopyOne() != InterCopyResult::Success) {                               //                                                L2: ... mt u uv
                    raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
                }
                lua_pop(L1, 1);                                                                    // L1: ... u [uv]*
                // this pops the value from the stack
                lua_setiuservalue(L2, -2, _uvi);                                                   //                                                L2: ... mt u
                --_uvi;
            }
            // when we are done, all uservalues are popped from the stack, we can pop the source as well
            lua_pop(L1, 1);                                                                        // L1: ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 2);                                                                    //                                                L2: ... mt u
        }
        // perform the custom cloning part
        lua_insert(L2, -2);                                                                        //                                                L2: ... u mt
        // __lanesclone should always exist because we wouldn't be restoring data from a userdata_clone_sentinel closure to begin with
        LuaType const _funcType{ luaG_getfield(L2, -1, "__lanesclone") };                          //                                                L2: ... u mt __lanesclone
        if (_funcType != LuaType::FUNCTION) {
            raise_luaL_error(getErrL(), "INTERNAL ERROR: __lanesclone is a %s, not a function", luaG_typename(L2, _funcType).data());
        }
        lua_remove(L2, -2);                                                                        //                                                L2: ... u __lanesclone
        lua_pushlightuserdata(L2, _clone);                                                         //                                                L2: ... u __lanesclone clone
        lua_pushlightuserdata(L2, _source);                                                        //                                                L2: ... u __lanesclone clone source
        lua_pushinteger(L2, userdata_size);                                                        //                                                L2: ... u __lanesclone clone source size
        // clone:__lanesclone(dest, source, size)
        lua_call(L2, 3, 0);                                                                        //                                                L2: ... u
    } else { // regular function
        DEBUGSPEW_CODE(DebugSpew(U) << "FUNCTION " << name << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U });
        copyCachedFunction();                                                                      //                                                L2: ... f
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyLightuserdata() const
{
    void* const _p{ lua_touserdata(L1, L1_i) };
    // recognize and print known UniqueKey names here
    if constexpr (USE_DEBUG_SPEW()) {
        bool _found{ false };
        static constexpr std::array<std::reference_wrapper<UniqueKey const>, 3> kKeysToCheck{ kLindaBatched, kCancelError, kNilSentinel };
        for (UniqueKey const& _key : kKeysToCheck) {
            if (_key.equals(L1, L1_i)) {
                DEBUGSPEW_CODE(DebugSpew(nullptr) << _key.debugName);
                _found = true;
                break;
            }
        }
        if (!_found) {
            DEBUGSPEW_CODE(DebugSpew(nullptr) << _p);
        }
    }
    // when copying a nil sentinel in a non-keeper, write a nil in the destination
    if (mode != LookupMode::ToKeeper && kNilSentinel.equals(L1, L1_i)) {
        DEBUGSPEW_CODE(DebugSpew(nullptr) << " as nil" << std::endl);
        lua_pushnil(L2);
    } else {
        lua_pushlightuserdata(L2, _p);
        DEBUGSPEW_CODE(DebugSpew(nullptr) << std::endl);
    }
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyNil() const
{
    if (vt == VT::KEY) {
        return false;
    }
    // when copying a nil in a keeper, write a nil sentinel in the destination
    if (mode == LookupMode::ToKeeper) {
        kNilSentinel.pushKey(L2);
    } else {
        lua_pushnil(L2);
    }
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyNumber() const
{
    // LNUM patch support (keeping integer accuracy)
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
    if (lua_isinteger(L1, L1_i)) {
        lua_Integer const _v{ lua_tointeger(L1, L1_i) };
        DEBUGSPEW_CODE(DebugSpew(nullptr) << _v << std::endl);
        lua_pushinteger(L2, _v);
    } else
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
    {
        lua_Number const _v{ lua_tonumber(L1, L1_i) };
        DEBUGSPEW_CODE(DebugSpew(nullptr) << _v << std::endl);
        lua_pushnumber(L2, _v);
    }
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyString() const
{
    std::string_view const _s{ luaG_tostring(L1, L1_i) };
    DEBUGSPEW_CODE(DebugSpew(nullptr) << "'" << _s << "'" << std::endl);
    luaG_pushstring(L2, _s);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyTable() const
{
    if (vt == VT::KEY) {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(DebugSpew(nullptr) << "TABLE " << name << std::endl);

    /*
     * First, let's try to see if this table is special (aka is it some table that we registered in our lookup databases during module registration?)
     * Note that this table CAN be a module table, but we just didn't register it, in which case we'll send it through the table cloning mechanism
     */
    if (lookupTable()) {
        LUA_ASSERT(L1, lua_istable(L2, -1) || (lua_tocfunction(L2, -1) == table_lookup_sentinel)); // from lookup data. can also be table_lookup_sentinel if this is a table we know
        return true;
    }

    /* Check if we've already copied the same table from 'L1' (during this transmission), and
     * reuse the old copy. This allows table upvalues shared by multiple
     * local functions to point to the same table, also in the target.
     * Also, this takes care of cyclic tables and multiple references
     * to the same subtable.
     *
     * Note: Even metatables need to go through this test; to detect
     *       loops such as those in required module tables (getmetatable(lanes).lanes == lanes)
     */
    if (pushCachedTable()) {                                                                       //                                                L2: ... t
        LUA_ASSERT(L1, lua_istable(L2, -1)); // from cache
        return true;
    }
    LUA_ASSERT(L1, lua_istable(L2, -1));

    STACK_GROW(L1, 2);
    STACK_GROW(L2, 2);

    lua_pushnil(L1); // start iteration
    while (lua_next(L1, L1_i)) {
        // need a function to prevent overflowing the stack with verboseErrors-induced alloca()
        interCopyKeyValuePair();
        lua_pop(L1, 1); // pop value (next round)
    }
    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 1);

    // Metatables are expected to be immutable, and copied only once.
    if (pushCachedMetatable()) {                                                                   //                                                L2: ... t mt?
        lua_setmetatable(L2, -2);                                                                  //                                                L2: ... t
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::interCopyUserdata() const
{
    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    if (vt == VT::KEY) {
        return false;
    }

    // try clonable userdata first
    if (tryCopyClonable()) {
        STACK_CHECK(L1, 0);
        STACK_CHECK(L2, 1);
        return true;
    }

    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 0);

    // Allow only deep userdata entities to be copied across
    if (tryCopyDeep()) {
        STACK_CHECK(L1, 0);
        STACK_CHECK(L2, 1);
        return true;
    }

    raise_luaL_error(getErrL(), "can't copy non-deep full userdata across lanes");
}

// #################################################################################################

#if USE_DEBUG_SPEW()
namespace {
    namespace local {
        static std::string_view const sLuaTypeNames[] = {
              "LUA_TNIL"
            , "LUA_TBOOLEAN"
            , "LUA_TLIGHTUSERDATA"
            , "LUA_TNUMBER"
            , "LUA_TSTRING"
            , "LUA_TTABLE"
            , "LUA_TFUNCTION"
            , "LUA_TUSERDATA"
            , "LUA_TTHREAD"
            , "<LUA_NUMTAGS>" // not really a type
            , "LUA_TJITCDATA" // LuaJIT specific
        };
        static std::string_view const sValueTypeNames[] = {
              "VT::NORMAL"
            , "VT::KEY"
            , "VT::METATABLE"
        };
    }
}
#endif // USE_DEBUG_SPEW()

/*
 * Copies a value from 'L1' state (at index 'i') to 'L2' state. Does not remove
 * the original value.
 *
 * NOTE: Both the states must be solely in the current OS thread's possession.
 *
 * 'i' is an absolute index (no -1, ...)
 *
 * Returns true if value was pushed, false if its type is non-supported.
 */
[[nodiscard]] InterCopyResult InterCopyContext::interCopyOne() const
{
    STACK_GROW(L2, 1);
    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    DEBUGSPEW_CODE(DebugSpew(U) << "interCopyOne()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U });

    // replace the value at L1_i with the result of a conversion if required
    LuaType const _val_type{ processConversion() };
    STACK_CHECK(L1, 0);
    DEBUGSPEW_CODE(DebugSpew(U) << local::sLuaTypeNames[static_cast<int>(_val_type)] << " " << local::sValueTypeNames[static_cast<int>(vt)] << ": ");

    // Lets push nil to L2 if the object should be ignored
    bool _ret{ true };
    switch (_val_type) {
    // Basic types allowed both as values, and as table keys
    case LuaType::BOOLEAN:
        _ret = interCopyBoolean();
        break;
    case LuaType::NUMBER:
        _ret = interCopyNumber();
        break;
    case LuaType::STRING:
        _ret = interCopyString();
        break;
    case LuaType::LIGHTUSERDATA:
        _ret = interCopyLightuserdata();
        break;

    // The following types are not allowed as table keys
    case LuaType::USERDATA:
        _ret = interCopyUserdata();
        break;
    case LuaType::NIL:
        _ret = interCopyNil();
        break;
    case LuaType::FUNCTION:
        _ret = interCopyFunction();
        break;
    case LuaType::TABLE:
        _ret = interCopyTable();
        break;

    // The following types cannot be copied
    case LuaType::NONE:
    case LuaType::CDATA:
        [[fallthrough]];
    case LuaType::THREAD:
        _ret = false;
        break;
    }

    STACK_CHECK(L2, _ret ? 1 : 0);
    STACK_CHECK(L1, 0);
    return _ret ? InterCopyResult::Success : InterCopyResult::Error;
}

// #################################################################################################

// transfers stuff from L1->_G["package"] to L2->_G["package"]
// returns InterCopyResult::Success if everything is fine
// returns InterCopyResult::Error if pushed an error message in L1
// else raise an error in whichever state is not a keeper
[[nodiscard]] InterCopyResult InterCopyContext::interCopyPackage() const
{
    DEBUGSPEW_CODE(DebugSpew(U) << "InterCopyContext::interCopyPackage()" << std::endl);

    class OnExit
    {
        private:
        lua_State* const L2;
        int const top_L2;
        DEBUGSPEW_CODE(DebugSpewIndentScope scope);

        public:
        OnExit(lua_State* L2_)
        : L2{ L2_ }
        , top_L2{ lua_gettop(L2) }
        DEBUGSPEW_COMMA_PARAM(scope{ Universe::Get(L2_) })
        {
        }

        ~OnExit()
        {
            lua_settop(L2, top_L2);
        }
    } const _onExit{ L2 };

    STACK_CHECK_START_REL(L1, 0);
    if (luaG_type(L1, L1_i) != LuaType::TABLE) {
        std::string_view const _msg{ luaG_pushstring(L1, "expected package as table, got a %s", luaL_typename(L1, L1_i)) };
        STACK_CHECK(L1, 1);
        // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
        if (mode == LookupMode::LaneBody) {
            raise_luaL_error(getErrL(), _msg);
        }
        return InterCopyResult::Error;
    }
    if (luaG_getmodule(L2, LUA_LOADLIBNAME) == LuaType::NIL) { // package library not loaded: do nothing
        DEBUGSPEW_CODE(DebugSpew(U) << "'package' not loaded, nothing to do" << std::endl);
        STACK_CHECK(L1, 0);
        return InterCopyResult::Success;
    }

    InterCopyResult _result{ InterCopyResult::Success };
    // package.loaders is renamed package.searchers in Lua 5.2
    // but don't copy it anyway, as the function names change depending on the slot index!
    // users should provide an on_state_create function to setup custom loaders instead
    // don't copy package.preload in keeper states (they don't know how to translate functions)
    std::string_view const _entries[] = { "path", "cpath", (mode == LookupMode::LaneBody) ? "preload" : "" /*, (LUA_VERSION_NUM == 501) ? "loaders" : "searchers"*/, "" };
    for (std::string_view const& _entry : _entries) {
        if (_entry.empty()) {
            continue;
        }
        DEBUGSPEW_CODE(DebugSpew(U) << "package." << _entry << std::endl);
        if (luaG_getfield(L1, L1_i, _entry) == LuaType::NIL) {
            lua_pop(L1, 1);
        } else {
            {
                DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U });
                // to move, we need a context with L1_i set to 0
                InterCopyContext _c{ U, L2, L1, L2_cache_i, {}, vt, mode, name };
                _result = _c.interMove(1); // moves the entry to L2
                STACK_CHECK(L1, 0);
            }
            if (_result == InterCopyResult::Success) {
                luaG_setfield(L2, -2, _entry); // set package[entry]
            } else {
                std::string_view const _msg{ luaG_pushstring(L1, "failed to copy package.%s", _entry.data()) };
                // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
                if (mode == LookupMode::LaneBody) {
                    raise_luaL_error(getErrL(), _msg);
                }
                lua_pop(L1, 1);
                break;
            }
        }
    }
    STACK_CHECK(L1, 0);
    return _result;
}

// #################################################################################################

// Akin to 'lua_xmove' but copies values between _any_ Lua states.
// NOTE: Both the states must be solely in the current OS thread's possession.
[[nodiscard]] InterCopyResult InterCopyContext::interCopy(int const n_) const
{
    LUA_ASSERT(L1, vt == VT::NORMAL);

    DEBUGSPEW_CODE(DebugSpew(U) << "InterCopyContext::interCopy()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U });

    int const _top_L1{ lua_gettop(L1) };
    int const _available{ (L1_i != 0) ? (_top_L1 - L1_i + 1) : _top_L1 };
    if (n_ > _available) {
        // requesting to copy more than is available?
        DEBUGSPEW_CODE(DebugSpew(U) << "nothing to copy" << std::endl);
        return InterCopyResult::NotEnoughValues;
    }

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, n_ + 1);

    /*
     * Make a cache table for the duration of this copy. Collects tables and
     * function entries, avoiding the same entries to be passed on as multiple
     * copies. ESSENTIAL i.e. for handling upvalue tables in the right manner!
     */
    int const _top_L2{ lua_gettop(L2) };                                                           //                                                L2: ...
    lua_newtable(L2);                                                                              //                                                L2: ... cache

    InterCopyContext _c{ U, L2, L1, CacheIndex{ _top_L2 + 1 }, {}, VT::NORMAL, mode, "?" };
    InterCopyResult _copyok{ InterCopyResult::Success };
    STACK_CHECK_START_REL(L1, 0);
    // if L1_i is specified, start here, else take the _n items off the top of the stack
    for (int _i{ L1_i != 0 ? L1_i : (_top_L1 - n_ + 1) }, _j{ 1 }; _j <= n_; ++_i, ++_j) {
        char _tmpBuf[16];
        if (U->verboseErrors) {
            sprintf(_tmpBuf, "arg_%d", _j);
            _c.name = _tmpBuf;
        }
        _c.L1_i = SourceIndex{ _i };
        _copyok = _c.interCopyOne();                                                               //                                                L2: ... cache {}n
        if (_copyok != InterCopyResult::Success) {
            break;
        }
    }
    STACK_CHECK(L1, 0);

    if (_copyok == InterCopyResult::Success) {
        STACK_CHECK(L2, n_ + 1);
        // Remove the cache table. Persistent caching would cause i.e. multiple
        // messages passed in the same table to use the same table also in receiving end.
        lua_remove(L2, _c.L2_cache_i);                                                             //                                                L2: ... {}n
        return InterCopyResult::Success;
    }

    // error -> pop everything from the target state stack
    lua_settop(L2, _top_L2);
    STACK_CHECK(L2, 0);
    return InterCopyResult::Error;
}

// #################################################################################################

[[nodiscard]] InterCopyResult InterCopyContext::interMove(int const n_) const
{
    assert(L1_i == 0); // we can only move stuff off the top of the stack
    InterCopyResult const _ret{ interCopy(n_) };
    lua_pop(L1, n_);
    return _ret;
}
