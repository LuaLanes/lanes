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

// Return some name helping to identify an object
[[nodiscard]]
static int DiscoverObjectNameRecur(lua_State* const L_, int shortest_, TableIndex const curDepth_)
{
    static constexpr StackIndex kWhat{ 1 }; // the object to investigate                           // L_: o "r" {c} {fqn} ... {?}
    static constexpr StackIndex kResult{ 2 }; // where the result string is stored
    static constexpr StackIndex kCache{ 3 }; // a cache
    static constexpr StackIndex kFQN{ 4 }; // the name compositing stack
    // no need to scan this table if the name we will discover is longer than one we already know
    TableIndex const _nextDepth{ curDepth_ + 1 };
    if (shortest_ <= _nextDepth) {
        return shortest_;
    }

    auto _pushNameOnFQN = [L_](std::string_view const& name_, TableIndex const depth_) {
        luaG_pushstring(L_, name_);                                                                // L_: o "r" {c} {fqn} ... name_
        lua_rawseti(L_, kFQN, depth_);                                                             // L_: o "r" {c} {fqn} ... {mt}
    };

    auto _popNameFromFQN = [L_](TableIndex const depth_) {
        lua_pushnil(L_);                                                                           // L_: o "r" {c} {fqn} ... nil
        lua_rawseti(L_, kFQN, depth_);                                                             // L_: o "r" {c} {fqn} ...
    };

    auto _recurseIfTableThenPop = [&_pushNameOnFQN, &_popNameFromFQN, L_](std::string_view const& name_, TableIndex const depth_, int shortest_) {
        STACK_CHECK_START_REL(L_, 0);                                                              // L_: o "r" {c} {fqn} ... <> {}?
        if (lua_istable(L_, kIdxTop)) {
            _pushNameOnFQN(name_, depth_);
            shortest_ = DiscoverObjectNameRecur(L_, shortest_, TableIndex{ depth_ + 1 });
            _popNameFromFQN(depth_);
        }
        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ... <>
        STACK_CHECK(L_, -1);
        return shortest_;
    };

    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    // stack top contains the table to search in
    LUA_ASSERT(L_, lua_istable(L_, kIdxTop));
    lua_pushvalue(L_, kIdxTop);                                                                    // L_: o "r" {c} {fqn} ... {?} {?}
    lua_rawget(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?} nil/1
    // if table is already visited, we are done
    if (!lua_isnil(L_, kIdxTop)) {
        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ... {?}
        return shortest_;
    }
    // examined table is not in the cache, add it now
    lua_pop(L_, 1);                                                                                // L_: o "r" {c} {fqn} ... {?}
    // cache[o] = 1
    lua_pushvalue(L_, kIdxTop);                                                                    // L_: o "r" {c} {fqn} ... {?} {?}
    lua_pushinteger(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... {?} {?} 1
    lua_rawset(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?}
    // scan table contents
    lua_pushnil(L_);                                                                               // L_: o "r" {c} {fqn} ... {?} nil
    while (lua_next(L_, -2)) {                                                                     // L_: o "r" {c} {fqn} ... {?} k v
        // std::string_view const _strKey{ (luaG_type(L_, -2) == LuaType::STRING) ? luaG_tostring(L_, -2) : "" }; // only for debugging
        // lua_Number const numKey = (luaG_type(L_, -2) == LuaType::NUMBER) ? lua_tonumber(L_, -2) : -6666; // only for debugging
        STACK_CHECK(L_, 2);
        // append key name to fqn stack
        lua_pushvalue(L_, -2);                                                                     // L_: o "r" {c} {fqn} ... {?} k v k
        lua_rawseti(L_, kFQN, _nextDepth);                                                         // L_: o "r" {c} {fqn} ... {?} k v
        if (lua_rawequal(L_, kIdxTop, kWhat)) { // is it what we are looking for?
            STACK_CHECK(L_, 2);
            // update shortest name
            if (_nextDepth < shortest_) {
                shortest_ = _nextDepth;
                std::ignore = tools::PushFQN(L_, kFQN, _nextDepth);                                // L_: o "r" {c} {fqn} ... {?} k v "fqn"
                lua_replace(L_, kResult);                                                          // L_: o "r" {c} {fqn} ... {?} k v
            }
            // no need to search further at this level
            lua_pop(L_, 2);                                                                        // L_: o "r" {c} {fqn} ... {?}
            STACK_CHECK(L_, 0);
            break;
        }
        switch (luaG_type(L_, kIdxTop)) {                                                          // L_: o "r" {c} {fqn} ... {?} k v
        default: // nil, boolean, light userdata, number and string aren't identifiable
            break;

        case LuaType::TABLE:                                                                       // L_: o "r" {c} {fqn} ... {?} k {}
            STACK_CHECK(L_, 2);
            shortest_ = DiscoverObjectNameRecur(L_, shortest_, _nextDepth);
            // search in the table's metatable too
            if (lua_getmetatable(L_, -1)) {                                                        // L_: o "r" {c} {fqn} ... {?} k {} {mt}
                shortest_ = _recurseIfTableThenPop("__metatable", _nextDepth, shortest_);          // L_: o "r" {c} {fqn} ... {?} k {}
            }
            STACK_CHECK(L_, 2);
            break;

        case LuaType::THREAD:                                                                      // L_: o "r" {c} {fqn} ... {?} k T
            // TODO: explore the thread's stack frame looking for our culprit?
            break;

        case LuaType::USERDATA:                                                                    // L_: o "r" {c} {fqn} ... {?} k U
            STACK_CHECK(L_, 2);
            // search in the object's metatable (some modules are built that way)
            if (lua_getmetatable(L_, -1)) {                                                        // L_: o "r" {c} {fqn} ... {?} k U {mt}
                shortest_ = _recurseIfTableThenPop("__metatable", _nextDepth, shortest_);          // L_: o "r" {c} {fqn} ... {?} k U
            }

            STACK_CHECK(L_, 2);
            // search in the object's uservalues
            {
                UserValueIndex _uvi{ 1 };
                while (lua_getiuservalue(L_, kIdxTop, _uvi) != LUA_TNONE) {                        // L_: o "r" {c} {fqn} ... {?} k U {u}
                    shortest_ = _recurseIfTableThenPop("uservalue", _nextDepth, shortest_);        // L_: o "r" {c} {fqn} ... {?} k U
                    ++_uvi;
                }
                // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
                lua_pop(L_, 1);                                                                    // L_: o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L_, 2);
            break;

        case LuaType::FUNCTION:                                                                    // L_: o "r" {c} {fqn} ... {?} k F
            // TODO: explore the function upvalues
            break;
        }
        // make ready for next iteration
        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ... {?} k
        // remove name from fqn stack
        _popNameFromFQN(_nextDepth);
        STACK_CHECK(L_, 1);
    }                                                                                              // L_: o "r" {c} {fqn} ... {?}
    STACK_CHECK(L_, 0);
    // remove the visited table from the cache, in case a shorter path to the searched object exists
    lua_pushvalue(L_, kIdxTop);                                                                    // L_: o "r" {c} {fqn} ... {?} {?}
    lua_pushnil(L_);                                                                               // L_: o "r" {c} {fqn} ... {?} {?} nil
    lua_rawset(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?}
    STACK_CHECK(L_, 0);
    return shortest_;
}

// #################################################################################################

// "type", "name" = lanes.nameof(o)
LUAG_FUNC(nameof)
{
    auto const _argCount{ lua_gettop(L_) };
    if (_argCount > 1) {
        raise_luaL_argerror(L_, StackIndex{ _argCount }, "too many arguments.");
    }

    // nil, boolean, light userdata, number and string aren't identifiable
    auto const _isIdentifiable = [L_]() {
        auto const _valType{ luaG_type(L_, kIdxTop) };
        return _valType == LuaType::TABLE || _valType == LuaType::FUNCTION || _valType == LuaType::USERDATA || _valType == LuaType::THREAD;
    };
    if (!_isIdentifiable()) {
        luaG_pushstring(L_, luaG_typename(L_, kIdxTop));                                           // L_: o "type"
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
    luaG_pushstring(L_, LUA_GNAME);                                                                // L_: o nil {c} {fqn} "_G"
    lua_rawseti(L_, -2, 1);                                                                        // L_: o nil {c} {fqn}
    // this is where we start the search
    luaG_pushglobaltable(L_);                                                                      // L_: o nil {c} {fqn} _G
    std::ignore = DiscoverObjectNameRecur(L_, std::numeric_limits<int>::max(), TableIndex{ 1 });
    if (lua_isnil(L_, 2)) { // try again with registry, just in case...
        lua_pop(L_, 1);                                                                            // L_: o nil {c} {fqn}
        luaG_pushstring(L_, "_R");                                                                 // L_: o nil {c} {fqn} "_R"
        lua_rawseti(L_, -2, 1);                                                                    // L_: o nil {c} {fqn}
        lua_pushvalue(L_, kIdxRegistry);                                                           // L_: o nil {c} {fqn} _R
        std::ignore = DiscoverObjectNameRecur(L_, std::numeric_limits<int>::max(), TableIndex{ 1 });
    }
    lua_pop(L_, 3);                                                                                // L_: o "result"
    STACK_CHECK(L_, 1);
    lua_pushstring(L_, luaL_typename(L_, 1));                                                      // L_: o "result" "type"
    lua_replace(L_, -3);                                                                           // L_: "type" "result"
    return 2;
}
