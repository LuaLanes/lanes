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

#include "_pch.hpp"
#include "nameof.hpp"

#include "tools.hpp"

// #################################################################################################

DECLARE_UNIQUE_TYPE(FqnLength, decltype(lua_rawlen(nullptr, kIdxTop)));

// Return some name helping to identify an object
[[nodiscard]]
FqnLength DiscoverObjectNameRecur(lua_State* const L_, FqnLength const shortest_)
{
    static constexpr StackIndex kWhat{ 1 }; // the object to investigate                           // L_: o "r" {c} {fqn} ... <>
    static constexpr StackIndex kResult{ 2 }; // where the result string is stored
    static constexpr StackIndex kCache{ 3 }; // a cache, where visited locations remember the FqnLength to reach them
    static constexpr StackIndex kFQN{ 4 }; // the name compositing stack

    // no need to scan this location if the name we will discover is longer than one we already know
    FqnLength const _fqnLength{ lua_rawlen(L_, kFQN) };
    if (shortest_ <= _fqnLength) {
        return shortest_;
    }

    // in: k, v at the top of the stack
    static constexpr auto _pushNameOnFQN = [](lua_State* const L_) {
        STACK_CHECK_START_REL(L_, 0);
        lua_pushvalue(L_, -2);                                                                     // L_: o "r" {c} {fqn} ... k v k
        auto const _keyType{ luaW_type(L_, kIdxTop) };
        if (_keyType != LuaType::STRING) {
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... k v
            luaW_pushstring(L_, "<%s>", luaW_typename(L_, _keyType).data());                       // L_: o "r" {c} {fqn} ... k v "<type of k>"
        } else {
            // decorate the key string with something that tells us the type of the value
            switch (luaW_type(L_, StackIndex{ -2 })) {
            default:
                LUA_ASSERT(L_, false); // there is something wrong if we end up here
                luaW_pushstring(L_, "??");                                                         // L_: o "r" {c} {fqn} ... k v "k" "??"
                break;
            case LuaType::FUNCTION:
                luaW_pushstring(L_, "()");                                                         // L_: o "r" {c} {fqn} ... k v "k" "()"
                break;
            case LuaType::TABLE:
                luaW_pushstring(L_, "[]");                                                         // L_: o "r" {c} {fqn} ... k v "k" "[]"
                break;
            case LuaType::USERDATA:
                luaW_pushstring(L_, "<>");                                                         // L_: o "r" {c} {fqn} ... k v "k" "<>"
                break;
            }
            lua_concat(L_, 2);                                                                     // L_: o "r" {c} {fqn} ... k v "k??"
        }

        FqnLength const _depth{ lua_rawlen(L_, kFQN) + 1 };
        lua_rawseti(L_, kFQN, static_cast<int>(_depth));                                           // L_: o "r" {c} {fqn} ... k v
        STACK_CHECK(L_, 0);
        return _depth;
    };

    static constexpr auto _popNameFromFQN = [](lua_State* const L_) {
        STACK_CHECK_START_REL(L_, 0);
        lua_pushnil(L_);                                                                           // L_: o "r" {c} {fqn} ... nil
        lua_rawseti(L_, kFQN, static_cast<int>(lua_rawlen(L_, kFQN)));                             // L_: o "r" {c} {fqn} ...
        STACK_CHECK(L_, 0);
    };

    static constexpr auto _recurseThenPop = [](lua_State* const L_, FqnLength const shortest_) -> FqnLength {
        STACK_CHECK_START_REL(L_, 0);                                                              // L_: o "r" {c} {fqn} ... <>
        FqnLength r_{ shortest_ };
        auto const _type{ luaW_type(L_, kIdxTop) };
        if (_type == LuaType::TABLE || _type == LuaType::USERDATA || _type == LuaType::FUNCTION) {
            r_ = DiscoverObjectNameRecur(L_, shortest_);
            STACK_CHECK(L_, 0);
            _popNameFromFQN(L_);
            STACK_CHECK(L_, 0);
        }

        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ...
        STACK_CHECK(L_, -1);
        return r_;
    };

    // in: k, v at the top of the stack
    // out: v popped from the stack
    static constexpr auto _processKeyValue = [](lua_State* const L_, FqnLength const shortest_) -> FqnLength {
        FqnLength _r{ shortest_ };
        STACK_GROW(L_, 2);
        STACK_CHECK_START_REL(L_, 0);                                                              // L_: o "r" {c} {fqn} ... k v

        // filter out uninteresting values
        auto const _valType{ luaW_type(L_, kIdxTop) };
        if (_valType == LuaType::NIL || _valType == LuaType::BOOLEAN || _valType == LuaType::LIGHTUSERDATA || _valType == LuaType::NUMBER || _valType == LuaType::STRING) {
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... k
            return _r;
        }

        // append key name to fqn stack
        FqnLength const _depth{ _pushNameOnFQN(L_) };                                              // L_: o "r" {c} {fqn} ... k v

        // process the value
        if (lua_rawequal(L_, kIdxTop, kWhat)) { // is it what we are looking for?
            // update shortest name
            if (_depth < _r) {
                _r = _depth;
                std::ignore = tools::PushFQN(L_, kFQN);                                            // L_: o "r" {c} {fqn} ... k v "fqn"
                lua_replace(L_, kResult);                                                          // L_: o "r" {c} {fqn} ... k v
            }

            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... k
            _popNameFromFQN(L_);
        } else {
            // let's see if the value contains what we are looking for
            _r = _recurseThenPop(L_, _r);                                                          // L_: o "r" {c} {fqn} ... k
        }

        STACK_CHECK(L_, -1);
        return _r;
    };

    static constexpr auto _scanTable = [](lua_State* const L_, FqnLength const shortest_) -> FqnLength {
        FqnLength r_{ shortest_ };
        STACK_GROW(L_, 2);
        STACK_CHECK_START_REL(L_, 0);
        lua_pushnil(L_);                                                                           // L_: o "r" {c} {fqn} ... {?} nil
        while (lua_next(L_, -2)) {                                                                 // L_: o "r" {c} {fqn} ... {?} k v
            r_ = _processKeyValue(L_, r_);                                                         // L_: o "r" {c} {fqn} ... {?} k
        }                                                                                          // L_: o "r" {c} {fqn} ... {?}

        if (lua_getmetatable(L_, kIdxTop)) {                                                       // L_: o "r" {c} {fqn} ... {?} {mt}
            lua_pushstring(L_, "__metatable");                                                     // L_: o "r" {c} {fqn} ... {?} {mt} "__metatable"
            lua_insert(L_, -2);                                                                    // L_: o "r" {c} {fqn} ... {?} "__metatable" {mt}
            r_ = _processKeyValue(L_, r_);                                                         // L_: o "r" {c} {fqn} ... {?} "__metatable"
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... {?}
        }

        STACK_CHECK(L_, 0);
        return r_;
    };

    static constexpr auto _scanUserData = [](lua_State* const L_, FqnLength const shortest_) -> FqnLength {
        FqnLength r_{ shortest_ };
        STACK_GROW(L_, 2);
        STACK_CHECK_START_REL(L_, 0);
        if (lua_getmetatable(L_, kIdxTop)) {                                                       // L_: o "r" {c} {fqn} ... U {mt}
            lua_pushstring(L_, "__metatable");                                                     // L_: o "r" {c} {fqn} ... U {mt} "__metatable"
            lua_insert(L_, -2);                                                                    // L_: o "r" {c} {fqn} ... U "__metatable" {mt}
            r_ = _processKeyValue(L_, r_);                                                         // L_: o "r" {c} {fqn} ... U "__metatable"
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... U
        }

        STACK_CHECK(L_, 0);

        UserValueIndex _uvi{ 0 };
        while (lua_getiuservalue(L_, kIdxTop, ++_uvi) != LUA_TNONE) {                              // L_: o "r" {c} {fqn} ... U uv
            luaW_pushstring(L_, "<uv:%d>", _uvi);                                                  // L_: o "r" {c} {fqn} ... U uv name
            lua_insert(L_, -2);                                                                    // L_: o "r" {c} {fqn} ... U name uv
            r_ = _processKeyValue(L_, r_);                                                         // L_: o "r" {c} {fqn} ... U name
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... U
        }

        // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ... U

        STACK_CHECK(L_, 0);
        return r_;
    };

    static constexpr auto _scanFunction = [](lua_State* const L_, FqnLength const shortest_) -> FqnLength {
        FqnLength r_{ shortest_ };
        STACK_GROW(L_, 2);
        STACK_CHECK_START_REL(L_, 0);                                                              // L_: o "r" {c} {fqn} ... F
        int _n{ 0 };
        for (char const* _upname{}; (_upname = lua_getupvalue(L_, kIdxTop, ++_n));) {              // L_: o "r" {c} {fqn} ... F up
            if (*_upname == 0) {
                _upname = "<C>";
            }

            luaW_pushstring(L_, "upvalue:%s", _upname);                                            // L_: o "r" {c} {fqn} ... F up name
            lua_insert(L_, -2);                                                                    // L_: o "r" {c} {fqn} ... F name up
            r_ = _processKeyValue(L_, r_);                                                         // L_: o "r" {c} {fqn} ... F name
            lua_pop(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... F
        }

        STACK_CHECK(L_, 0);
        return r_;
    };

    STACK_GROW(L_, 2);
    STACK_CHECK_START_REL(L_, 0);
    // stack top contains the location to search in (table, function, userdata)
    [[maybe_unused]] auto const _typeWhere{ luaW_type(L_, kIdxTop) };
    LUA_ASSERT(L_, _typeWhere == LuaType::TABLE || _typeWhere == LuaType::USERDATA || _typeWhere == LuaType::FUNCTION);
    lua_pushvalue(L_, kIdxTop);                                                                    // L_: o "r" {c} {fqn} ... <> <>
    lua_rawget(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... <> nil/N
    auto const _visitDepth{ lua_isnil(L_, kIdxTop) ? std::numeric_limits<FqnLength::type>::max() : lua_tointeger(L_, kIdxTop) };
    lua_pop(L_, 1);                                                                                // L_: o "r" {c} {fqn} ... <>
    // if location is already visited with a name of <= length, we are done
    if (_visitDepth <= _fqnLength) {
        return shortest_;
    }

    // examined location is not in the cache, add it now
    // cache[o] = _fqnLength
    lua_pushvalue(L_, kIdxTop);                                                                    // L_: o "r" {c} {fqn} ... <> <>
    lua_pushinteger(L_, _fqnLength);                                                               // L_: o "r" {c} {fqn} ... <> <> N
    lua_rawset(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... <>

    FqnLength r_;
    // scan location contents
    switch (_typeWhere) {                                                                          // L_: o "r" {c} {fqn} ... <>
    default:
        raise_luaL_error(L_, "unexpected error, please investigate");
        break;
    case LuaType::TABLE:
        r_ = _scanTable(L_, shortest_);
        break;
    case LuaType::USERDATA:
        r_ = _scanUserData(L_, shortest_);
        break;
    case LuaType::FUNCTION:
        r_ = _scanFunction(L_, shortest_);
        break;
    }

    STACK_CHECK(L_, 0);
    return r_;
}

// #################################################################################################

// "type", "name" = lanes.nameof(o)
LUAG_FUNC(nameof)
{
    auto const _argCount{ lua_gettop(L_) };
    if (_argCount != 1) {
        raise_luaL_argerror(L_, StackIndex{ _argCount }, "exactly 1 argument expected");
    }

    // nil, boolean, light userdata, number and string aren't identifiable
    static constexpr auto _isIdentifiable = [](lua_State* const L_) {
        auto const _valType{ luaW_type(L_, kIdxTop) };
        return _valType == LuaType::TABLE || _valType == LuaType::FUNCTION || _valType == LuaType::USERDATA || _valType == LuaType::THREAD;
    };

    if (!_isIdentifiable(L_)) {
        luaW_pushstring(L_, luaW_typename(L_, kIdxTop));                                           // L_: o "type"
        lua_insert(L_, -2);                                                                        // L_: "type" o
        return 2;
    }

    STACK_GROW(L_, 4);
    STACK_CHECK_START_REL(L_, 0);
    // this slot will contain the shortest name we found when we are done
    lua_pushnil(L_);                                                                               // L_: o nil
    // push a cache that will contain all already visited tables
    lua_newtable(L_);                                                                              // L_: o nil {c}
    // push a table whose contents are strings that, when concatenated, produce unique name
    lua_newtable(L_);                                                                              // L_: o nil {c} {fqn}
    // {fqn}[1] = "_G"
    luaW_pushstring(L_, LUA_GNAME);                                                                // L_: o nil {c} {fqn} "_G"
    lua_rawseti(L_, -2, 1);                                                                        // L_: o nil {c} {fqn}
    // this is where we start the search
    luaW_pushglobaltable(L_);                                                                      // L_: o nil {c} {fqn} _G
    auto const _foundInG{ DiscoverObjectNameRecur(L_, FqnLength{ std::numeric_limits<FqnLength::type>::max() }) };
    if (lua_isnil(L_, 2)) { // try again with registry, just in case...
        LUA_ASSERT(L_, _foundInG == std::numeric_limits<FqnLength::type>::max());
        lua_pop(L_, 1);                                                                            // L_: o nil {c} {fqn}
        luaW_pushstring(L_, "_R");                                                                 // L_: o nil {c} {fqn} "_R"
        lua_rawseti(L_, -2, 1);                                                                    // L_: o nil {c} {fqn}
        lua_pushvalue(L_, kIdxRegistry);                                                           // L_: o nil {c} {fqn} _R
        [[maybe_unused]] auto const _foundInR{ DiscoverObjectNameRecur(L_, FqnLength{ std::numeric_limits<FqnLength::type>::max() }) };
    }
    lua_pop(L_, 3);                                                                                // L_: o "result"
    STACK_CHECK(L_, 1);
    lua_pushstring(L_, luaL_typename(L_, 1));                                                      // L_: o "result" "type"
    lua_replace(L_, -3);                                                                           // L_: "type" "result"
    return 2;
}
