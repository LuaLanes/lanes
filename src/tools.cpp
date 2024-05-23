/*
 * TOOLS.CPP                         Copyright (c) 2002-10, Asko Kauppi
 *                                   Copyright (C) 2010-24, Benoit Germain
 *
 * Lua tools to support Lanes.
 */

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
              2011-24 benoit Germain <bnt.germain@gmail.com>

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

#include "tools.h"

#include "debugspew.h"
#include "universe.h"

DEBUGSPEW_CODE(char const* const DebugSpewIndentScope::debugspew_indent = "----+----!----+----!----+----!----+----!----+----!----+----!----+----!----+");

// xxh64 of string "kLookupCacheRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLookupCacheRegKey{ 0x9BF75F84E54B691Bull };

// #################################################################################################

static constexpr int kWriterReturnCode{ 666 };
[[nodiscard]] static int dummy_writer([[maybe_unused]] lua_State* L_, [[maybe_unused]] void const* p_, [[maybe_unused]] size_t sz_, [[maybe_unused]] void* ud_)
{
    return kWriterReturnCode;
}

/*
 * differentiation between C, bytecode and JIT-fast functions
 *
 *                   +-------------------+------------+----------+
 *                   |      bytecode     | C function | JIT-fast |
 * +-----------------+-------------------+------------+----------+
 * | lua_topointer   |                   |            |          |
 * +-----------------+-------------------+------------+----------+
 * | lua_tocfunction |      nullptr      |            |  nullptr |
 * +-----------------+-------------------+------------+----------+
 * | lua_dump        | kWriterReturnCode |  1         |  1       |
 * +-----------------+-------------------+------------+----------+
 */

[[nodiscard]] FuncSubType luaG_getfuncsubtype(lua_State* L_, int _i)
{
    if (lua_tocfunction(L_, _i)) { // nullptr for LuaJIT-fast && bytecode functions
        return FuncSubType::Native;
    }
    {
        int _mustpush{ 0 };
        if (lua_absindex(L_, _i) != lua_gettop(L_)) {
            lua_pushvalue(L_, _i);
            _mustpush = 1;
        }
        // the provided writer fails with code kWriterReturnCode
        // therefore, anytime we get kWriterReturnCode, this means that lua_dump() attempted a dump
        // all other cases mean this is either a C or LuaJIT-fast function
        int const _dumpres{ lua504_dump(L_, dummy_writer, nullptr, 0) };
        lua_pop(L_, _mustpush);
        if (_dumpres == kWriterReturnCode) {
            return FuncSubType::Bytecode;
        }
    }
    return FuncSubType::FastJIT;
}

// #################################################################################################

// inspired from tconcat() in ltablib.c
[[nodiscard]] static std::string_view luaG_pushFQN(lua_State* L_, int t_, int last_)
{
    luaL_Buffer _b;
    STACK_CHECK_START_REL(L_, 0);
    // Lua 5.4 pushes &b as light userdata on the stack. be aware of it...
    luaL_buffinit(L_, &_b);                                                                        // L_: ... {} ... &b?
    int _i{ 1 };
    for (; _i < last_; ++_i) {
        lua_rawgeti(L_, t_, _i);
        luaL_addvalue(&_b);
        luaL_addlstring(&_b, "/", 1);
    }
    if (_i == last_) { // add last value (if interval was not empty)
        lua_rawgeti(L_, t_, _i);
        luaL_addvalue(&_b);
    }
    // &b is popped at that point (-> replaced by the result)
    luaL_pushresult(&_b);                                                                          // L_: ... {} ... "<result>"
    STACK_CHECK(L_, 1);
    return lua_tostringview(L_, -1);
}

// #################################################################################################

/*
 * receives 2 arguments: a name k and an object o
 * add two entries ["fully.qualified.name"] = o
 * and             [o] = "fully.qualified.name"
 * where <o> is either a table or a function
 * if we already had an entry of type [o] = ..., replace the name if the new one is shorter
 * pops the processed object from the stack
 */
static void update_lookup_entry(lua_State* L_, int ctxBase_, int depth_)
{
    // slot 1 in the stack contains the table that receives everything we found
    int const _dest{ ctxBase_ };
    // slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
    int const _fqn{ ctxBase_ + 1 };

    DEBUGSPEW_CODE(Universe* const _U{ universe_get(L_) });
    DEBUGSPEW_CODE(DebugSpew(_U) << "update_lookup_entry()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });

    STACK_CHECK_START_REL(L_, 0);
    // first, raise an error if the function is already known
    lua_pushvalue(L_, -1);                                                                         // L_: ... {bfc} k o o
    lua_rawget(L_, _dest);                                                                         // L_: ... {bfc} k o name?
    std::string_view const _prevName{ lua_tostringview(L_, -1) }; // nullptr if we got nil (first encounter of this object)
    // push name in fqn stack (note that concatenation will crash if name is a not string or a number)
    lua_pushvalue(L_, -3);                                                                         // L_: ... {bfc} k o name? k
    LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TNUMBER || lua_type(L_, -1) == LUA_TSTRING);
    ++depth_;
    lua_rawseti(L_, _fqn, depth_);                                                                 // L_: ... {bfc} k o name?
    // generate name
    std::string_view const _newName{ luaG_pushFQN(L_, _fqn, depth_) };                             // L_: ... {bfc} k o name? "f.q.n"
    // Lua 5.2 introduced a hash randomizer seed which causes table iteration to yield a different key order
    // on different VMs even when the tables are populated the exact same way.
    // When Lua is built with compatibility options (such as LUA_COMPAT_ALL),
    // this causes several base libraries to register functions under multiple names.
    // This, with the randomizer, can cause the first generated name of an object to be different on different VMs,
    // which breaks function transfer.
    // Also, nothing prevents any external module from exposing a given object under several names, so...
    // Therefore, when we encounter an object for which a name was previously registered, we need to select the names
    // based on some sorting order so that we end up with the same name in all databases whatever order the table walk yielded
    if (!_prevName.empty() && (_prevName.size() < _newName.size() || lua_lessthan(L_, -2, -1))) {
        DEBUGSPEW_CODE(DebugSpew(_U) << lua_typename(L_, lua_type(L_, -3)) << " '" << _newName << "' remains named '" << _prevName << "'" << std::endl);
        // the previous name is 'smaller' than the one we just generated: keep it!
        lua_pop(L_, 3);                                                                            // L_: ... {bfc} k
    } else {
        // the name we generated is either the first one, or a better fit for our purposes
        if (!_prevName.empty()) {
            // clear the previous name for the database to avoid clutter
            lua_insert(L_, -2);                                                                    // L_: ... {bfc} k o "f.q.n" prevName
            // t[prevName] = nil
            lua_pushnil(L_);                                                                       // L_: ... {bfc} k o "f.q.n" prevName nil
            lua_rawset(L_, _dest);                                                                 // L_: ... {bfc} k o "f.q.n"
        } else {
            lua_remove(L_, -2);                                                                    // L_: ... {bfc} k o "f.q.n"
        }
        DEBUGSPEW_CODE(DebugSpew(_U) << lua_typename(L_, lua_type(L_, -2)) << " '" << _newName << "'" << std::endl);
        // prepare the stack for database feed
        lua_pushvalue(L_, -1);                                                                     // L_: ... {bfc} k o "f.q.n" "f.q.n"
        lua_pushvalue(L_, -3);                                                                     // L_: ... {bfc} k o "f.q.n" "f.q.n" o
        LUA_ASSERT(L_, lua_rawequal(L_, -1, -4));
        LUA_ASSERT(L_, lua_rawequal(L_, -2, -3));
        // t["f.q.n"] = o
        lua_rawset(L_, _dest);                                                                     // L_: ... {bfc} k o "f.q.n"
        // t[o] = "f.q.n"
        lua_rawset(L_, _dest);                                                                     // L_: ... {bfc} k
        // remove table name from fqn stack
        lua_pushnil(L_);                                                                           // L_: ... {bfc} k nil
        lua_rawseti(L_, _fqn, depth_);                                                             // L_: ... {bfc} k
    }
    --depth_;
    STACK_CHECK(L_, -1);
}

// #################################################################################################

static void populate_func_lookup_table_recur(lua_State* L_, int dbIdx_, int i_, int depth_)
{
    // slot dbIdx_ contains the lookup database table
    // slot dbIdx_ + 1 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot i_
    int const _fqn{ dbIdx_ + 1 };
    // slot dbIdx_ + 2 contains a cache that stores all already visited tables to avoid infinite recursion loops
    int const _cache{ dbIdx_ + 2 };
    DEBUGSPEW_CODE(Universe* const _U{ universe_get(L_) });
    DEBUGSPEW_CODE(DebugSpew(_U) << "populate_func_lookup_table_recur()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });

    STACK_GROW(L_, 6);
    // slot i_ contains a table where we search for functions (or a full userdata with a metatable)
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: ... {i_}

    // if object is a userdata, replace it by its metatable
    if (lua_type(L_, i_) == LUA_TUSERDATA) {
        lua_getmetatable(L_, i_);                                                                  // L_: ... {i_} mt
        lua_replace(L_, i_);                                                                       // L_: ... {i_}
    }

    // if table is already visited, we are done
    lua_pushvalue(L_, i_);                                                                         // L_: ... {i_} {}
    lua_rawget(L_, _cache);                                                                        // L_: ... {i_} nil|n
    lua_Integer _visit_count{ lua_tointeger(L_, -1) }; // 0 if nil, else n
    lua_pop(L_, 1);                                                                                // L_: ... {i_}
    STACK_CHECK(L_, 0);
    if (_visit_count > 0) {
        DEBUGSPEW_CODE(DebugSpew(_U) << "already visited" << std::endl);
        return;
    }

    // remember we visited this table (1-visit count)
    lua_pushvalue(L_, i_);                                                                         // L_: ... {i_} {}
    lua_pushinteger(L_, _visit_count + 1);                                                         // L_: ... {i_} {} 1
    lua_rawset(L_, _cache);                                                                        // L_: ... {i_}
    STACK_CHECK(L_, 0);

    // we need to remember subtables to process them after functions encountered at the current depth (breadth-first search)
    lua_newtable(L_);                                                                              // L_: ... {i_} {bfc}
    int const breadthFirstCache{ lua_gettop(L_) };
    // iterate over all entries in the processed table
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    while (lua_next(L_, i_) != 0) {                                                                // L_: ... {i_} {bfc} k v
        // just for debug, not actually needed
        // char const* key = (lua_type(L, -2) == LUA_TSTRING) ? lua_tostring(L, -2) : "not a string";
        // subtable: process it recursively
        if (lua_istable(L_, -1)) {                                                                 // L_: ... {i_} {bfc} k {}
            // increment visit count to make sure we will actually scan it at this recursive level
            lua_pushvalue(L_, -1);                                                                 // L_: ... {i_} {bfc} k {} {}
            lua_pushvalue(L_, -1);                                                                 // L_: ... {i_} {bfc} k {} {} {}
            lua_rawget(L_, _cache);                                                                // L_: ... {i_} {bfc} k {} {} n?
            _visit_count = lua_tointeger(L_, -1) + 1; // 1 if we got nil, else n+1
            lua_pop(L_, 1);                                                                        // L_: ... {i_} {bfc} k {} {}
            lua_pushinteger(L_, _visit_count);                                                     // L_: ... {i_} {bfc} k {} {} n
            lua_rawset(L_, _cache);                                                                // L_: ... {i_} {bfc} k {}
            // store the table in the breadth-first cache
            lua_pushvalue(L_, -2);                                                                 // L_: ... {i_} {bfc} k {} k
            lua_pushvalue(L_, -2);                                                                 // L_: ... {i_} {bfc} k {} k {}
            lua_rawset(L_, breadthFirstCache);                                                     // L_: ... {i_} {bfc} k {}
            // generate a name, and if we already had one name, keep whichever is the shorter
            update_lookup_entry(L_, dbIdx_, depth_);                                               // L_: ... {i_} {bfc} k
        } else if (lua_isfunction(L_, -1) && (luaG_getfuncsubtype(L_, -1) != FuncSubType::Bytecode)) {
            // generate a name, and if we already had one name, keep whichever is the shorter
            // this pops the function from the stack
            update_lookup_entry(L_, dbIdx_, depth_);                                               // L_: ... {i_} {bfc} k
        } else {
            lua_pop(L_, 1); // L_: ... {i_} {bfc} k
        }
        STACK_CHECK(L_, 2);
    }
    // now process the tables we encountered at that depth
    ++depth_;
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    while (lua_next(L_, breadthFirstCache) != 0) {                                                 // L_: ... {i_} {bfc} k {}
        DEBUGSPEW_CODE(std::string_view const _key{ (lua_type(L_, -2) == LUA_TSTRING) ? lua_tostringview(L_, -2) : std::string_view{ "<not a string>" } });
        DEBUGSPEW_CODE(DebugSpew(_U) << "table '"<< _key <<"'" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope2{ _U });
        // un-visit this table in case we do need to process it
        lua_pushvalue(L_, -1);                                                                     // L_: ... {i_} {bfc} k {} {}
        lua_rawget(L_, _cache);                                                                    // L_: ... {i_} {bfc} k {} n
        LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TNUMBER);
        _visit_count = lua_tointeger(L_, -1) - 1;
        lua_pop(L_, 1);                                                                            // L_: ... {i_} {bfc} k {}
        lua_pushvalue(L_, -1);                                                                     // L_: ... {i_} {bfc} k {} {}
        if (_visit_count > 0) {
            lua_pushinteger(L_, _visit_count);                                                     // L_: ... {i_} {bfc} k {} {} n
        } else {
            lua_pushnil(L_);                                                                       // L_: ... {i_} {bfc} k {} {} nil
        }
        lua_rawset(L_, _cache);                                                                    // L_: ... {i_} {bfc} k {}
        // push table name in fqn stack (note that concatenation will crash if name is a not string!)
        lua_pushvalue(L_, -2);                                                                     // L_: ... {i_} {bfc} k {} k
        lua_rawseti(L_, _fqn, depth_);                                                             // L_: ... {i_} {bfc} k {}
        populate_func_lookup_table_recur(L_, dbIdx_, lua_gettop(L_), depth_);
        lua_pop(L_, 1);                                                                            // L_: ... {i_} {bfc} k
        STACK_CHECK(L_, 2);
    }
    // remove table name from fqn stack
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    lua_rawseti(L_, _fqn, depth_);                                                                 // L_: ... {i_} {bfc}
    --depth_;
    // we are done with our cache
    lua_pop(L_, 1);                                                                                // L_: ... {i_}
    STACK_CHECK(L_, 0);
    // we are done                                                                                 // L_: ... {i_} {bfc}
}

// #################################################################################################

// create a "fully.qualified.name" <-> function equivalence database
void populate_func_lookup_table(lua_State* const L_, int const i_, std::string_view const& name_)
{
    int const _in_base = lua_absindex(L_, i_);
    DEBUGSPEW_CODE(Universe* _U = universe_get(L_));
    std::string_view _name{ name_.empty() ? std::string_view{} : name_ };
    DEBUGSPEW_CODE(DebugSpew(_U) << L_ << ": populate_func_lookup_table('" << _name << "')" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    kLookupRegKey.pushValue(L_);                                                                   // L_: {}
    int const _dbIdx{ lua_gettop(L_) };
    STACK_CHECK(L_, 1);
    LUA_ASSERT(L_, lua_istable(L_, -1));
    if (lua_type(L_, _in_base) == LUA_TFUNCTION) { // for example when a module is a simple function
        if (_name.empty()) {
            _name = "nullptr";
        }
        lua_pushvalue(L_, _in_base);                                                               // L_: {} f
        std::ignore = lua_pushstringview(L_, _name);                                               // L_: {} f name_
        lua_rawset(L_, -3);                                                                        // L_: {}
        std::ignore = lua_pushstringview(L_, _name);                                               // L_: {} name_
        lua_pushvalue(L_, _in_base);                                                               // L_: {} name_ f
        lua_rawset(L_, -3);                                                                        // L_: {}
        lua_pop(L_, 1);                                                                            // L_:
    } else if (lua_type(L_, _in_base) == LUA_TTABLE) {
        lua_newtable(L_);                                                                          // L_: {} {fqn}
        int _startDepth{ 0 };
        if (!_name.empty()) {
            STACK_CHECK(L_, 2);
            std::ignore = lua_pushstringview(L_, _name);                                           // L_: {} {fqn} "name"
            // generate a name, and if we already had one name, keep whichever is the shorter
            lua_pushvalue(L_, _in_base);                                                           // L_: {} {fqn} "name" t
            update_lookup_entry(L_, _dbIdx, _startDepth);                                          // L_: {} {fqn} "name"
            // don't forget to store the name at the bottom of the fqn stack
            lua_rawseti(L_, -2, ++_startDepth);                                                    // L_: {} {fqn}
            STACK_CHECK(L_, 2);
        }
        // retrieve the cache, create it if we haven't done it yet
        std::ignore = kLookupCacheRegKey.getSubTable(L_, 0, 0);                                    // L_: {} {fqn} {cache}
        // process everything we find in that table, filling in lookup data for all functions and tables we see there
        populate_func_lookup_table_recur(L_, _dbIdx, _in_base, _startDepth);
        lua_pop(L_, 3);                                                                            // L_:
    } else {
        lua_pop(L_, 1);                                                                            // L_:
        raise_luaL_error(L_, "unsupported module type %s", lua_typename(L_, lua_type(L_, _in_base)));
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

// Return some name helping to identify an object
[[nodiscard]] static int DiscoverObjectNameRecur(lua_State* L_, int shortest_, int depth_)
{
    static constexpr int kWhat{ 1 }; // the object to investigate                                  // L_: o "r" {c} {fqn} ... {?}
    static constexpr int kResult{ 2 }; // where the result string is stored
    static constexpr int kCache{ 3 }; // a cache
    static constexpr int kFQN{ 4 }; // the name compositing stack
    // no need to scan this table if the name we will discover is longer than one we already know
    if (shortest_ <= depth_ + 1) {
        return shortest_;
    }
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    // stack top contains the table to search in
    lua_pushvalue(L_, -1);                                                                         // L_: o "r" {c} {fqn} ... {?} {?}
    lua_rawget(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?} nil/1
    // if table is already visited, we are done
    if (!lua_isnil(L_, -1)) {
        lua_pop(L_, 1); // L_: o "r" {c} {fqn} ... {?}
        return shortest_;
    }
    // examined table is not in the cache, add it now
    lua_pop(L_, 1);                                                                                // L_: o "r" {c} {fqn} ... {?}
    lua_pushvalue(L_, -1);                                                                         // L_: o "r" {c} {fqn} ... {?} {?}
    lua_pushinteger(L_, 1);                                                                        // L_: o "r" {c} {fqn} ... {?} {?} 1
    lua_rawset(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?}
    // scan table contents
    lua_pushnil(L_);                                                                               // L_: o "r" {c} {fqn} ... {?} nil
    while (lua_next(L_, -2)) {                                                                     // L_: o "r" {c} {fqn} ... {?} k v
        // char const *const strKey = (lua_type(L_, -2) == LUA_TSTRING) ? lua_tostring(L_, -2) : nullptr; // only for debugging
        // lua_Number const numKey = (lua_type(L_, -2) == LUA_TNUMBER) ? lua_tonumber(L_, -2) : -6666; // only for debugging
        STACK_CHECK(L_, 2);
        // append key name to fqn stack
        ++depth_;
        lua_pushvalue(L_, -2);                                                                     // L_: o "r" {c} {fqn} ... {?} k v k
        lua_rawseti(L_, kFQN, depth_);                                                             // L_: o "r" {c} {fqn} ... {?} k v
        if (lua_rawequal(L_, -1, kWhat)) { // is it what we are looking for?
            STACK_CHECK(L_, 2);
            // update shortest name
            if (depth_ < shortest_) {
                shortest_ = depth_;
                std::ignore = luaG_pushFQN(L_, kFQN, depth_);                                      // L_: o "r" {c} {fqn} ... {?} k v "fqn"
                lua_replace(L_, kResult);                                                          // L_: o "r" {c} {fqn} ... {?} k v
            }
            // no need to search further at this level
            lua_pop(L_, 2);                                                                        // L_: o "r" {c} {fqn} ... {?}
            STACK_CHECK(L_, 0);
            break;
        }
        switch (lua_type(L_, -1)) {                                                                // L_: o "r" {c} {fqn} ... {?} k v
        default: // nil, boolean, light userdata, number and string aren't identifiable
            break;

        case LUA_TTABLE:                                                                           // L_: o "r" {c} {fqn} ... {?} k {}
            STACK_CHECK(L_, 2);
            shortest_ = DiscoverObjectNameRecur(L_, shortest_, depth_);
            // search in the table's metatable too
            if (lua_getmetatable(L_, -1)) {                                                        // L_: o "r" {c} {fqn} ... {?} k {} {mt}
                if (lua_istable(L_, -1)) {
                    ++depth_;
                    lua_pushliteral(L_, "__metatable");                                            // L_: o "r" {c} {fqn} ... {?} k {} {mt} "__metatable"
                    lua_rawseti(L_, kFQN, depth_);                                                 // L_: o "r" {c} {fqn} ... {?} k {} {mt}
                    shortest_ = DiscoverObjectNameRecur(L_, shortest_, depth_);
                    lua_pushnil(L_);                                                               // L_: o "r" {c} {fqn} ... {?} k {} {mt} nil
                    lua_rawseti(L_, kFQN, depth_);                                                 // L_: o "r" {c} {fqn} ... {?} k {} {mt}
                    --depth_;
                }
                lua_pop(L_, 1);                                                                    // L_: o "r" {c} {fqn} ... {?} k {}
            }
            STACK_CHECK(L_, 2);
            break;

        case LUA_TTHREAD:                                                                          // L_: o "r" {c} {fqn} ... {?} k T
            // TODO: explore the thread's stack frame looking for our culprit?
            break;

        case LUA_TUSERDATA:                                                                        // L_: o "r" {c} {fqn} ... {?} k U
            STACK_CHECK(L_, 2);
            // search in the object's metatable (some modules are built that way)
            if (lua_getmetatable(L_, -1)) {                                                        // L_: o "r" {c} {fqn} ... {?} k U {mt}
                if (lua_istable(L_, -1)) {
                    ++depth_;
                    lua_pushliteral(L_, "__metatable");                                            // L_: o "r" {c} {fqn} ... {?} k U {mt} "__metatable"
                    lua_rawseti(L_, kFQN, depth_);                                                 // L_: o "r" {c} {fqn} ... {?} k U {mt}
                    shortest_ = DiscoverObjectNameRecur(L_, shortest_, depth_);
                    lua_pushnil(L_);                                                               // L_: o "r" {c} {fqn} ... {?} k U {mt} nil
                    lua_rawseti(L_, kFQN, depth_);                                                 // L_: o "r" {c} {fqn} ... {?} k U {mt}
                    --depth_;
                }
                lua_pop(L_, 1);                                                                    // L_: o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L_, 2);
            // search in the object's uservalues
            {
                int _uvi{ 1 };
                while (lua_getiuservalue(L_, -1, _uvi) != LUA_TNONE) {                             // L_: o "r" {c} {fqn} ... {?} k U {u}
                    if (lua_istable(L_, -1)) { // if it is a table, look inside
                        ++depth_;
                        lua_pushliteral(L_, "uservalue");                                          // L_: o "r" {c} {fqn} ... {?} k v {u} "uservalue"
                        lua_rawseti(L_, kFQN, depth_);                                             // L_: o "r" {c} {fqn} ... {?} k v {u}
                        shortest_ = DiscoverObjectNameRecur(L_, shortest_, depth_);
                        lua_pushnil(L_);                                                           // L_: o "r" {c} {fqn} ... {?} k v {u} nil
                        lua_rawseti(L_, kFQN, depth_);                                             // L_: o "r" {c} {fqn} ... {?} k v {u}
                        --depth_;
                    }
                    lua_pop(L_, 1);                                                                // L_: o "r" {c} {fqn} ... {?} k U
                    ++_uvi;
                }
                // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
                lua_pop(L_, 1);                                                                    // L_: o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L_, 2);
            break;
        }
        // make ready for next iteration
        lua_pop(L_, 1);                                                                            // L_: o "r" {c} {fqn} ... {?} k
        // remove name from fqn stack
        lua_pushnil(L_);                                                                           // L_: o "r" {c} {fqn} ... {?} k nil
        lua_rawseti(L_, kFQN, depth_);                                                             // L_: o "r" {c} {fqn} ... {?} k
        STACK_CHECK(L_, 1);
        --depth_;
    }                                                                                              // L_: o "r" {c} {fqn} ... {?}
    STACK_CHECK(L_, 0);
    // remove the visited table from the cache, in case a shorter path to the searched object exists
    lua_pushvalue(L_, -1);                                                                         // L_: o "r" {c} {fqn} ... {?} {?}
    lua_pushnil(L_);                                                                               // L_: o "r" {c} {fqn} ... {?} {?} nil
    lua_rawset(L_, kCache);                                                                        // L_: o "r" {c} {fqn} ... {?}
    STACK_CHECK(L_, 0);
    return shortest_;
}

// #################################################################################################

// "type", "name" = lanes.nameof(o)
int luaG_nameof(lua_State* L_)
{
    int const _what{ lua_gettop(L_) };
    if (_what > 1) {
        raise_luaL_argerror(L_, _what, "too many arguments.");
    }

    // nil, boolean, light userdata, number and string aren't identifiable
    if (lua_type(L_, 1) < LUA_TTABLE) {
        lua_pushstring(L_, luaL_typename(L_, 1));                                                  // L_: o "type"
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
    lua_pushliteral(L_, LUA_GNAME);                                                                // L_: o nil {c} {fqn} "_G"
    lua_rawseti(L_, -2, 1);                                                                        // L_: o nil {c} {fqn}
    // this is where we start the search
    lua_pushglobaltable(L_);                                                                       // L_: o nil {c} {fqn} _G
    std::ignore = DiscoverObjectNameRecur(L_, std::numeric_limits<int>::max(), 1);
    if (lua_isnil(L_, 2)) { // try again with registry, just in case...
        lua_pop(L_, 1);                                                                            // L_: o nil {c} {fqn}
        lua_pushliteral(L_, "_R");                                                                 // L_: o nil {c} {fqn} "_R"
        lua_rawseti(L_, -2, 1);                                                                    // L_: o nil {c} {fqn}
        lua_pushvalue(L_, LUA_REGISTRYINDEX);                                                      // L_: o nil {c} {fqn} _R
        std::ignore = DiscoverObjectNameRecur(L_, std::numeric_limits<int>::max(), 1);
    }
    lua_pop(L_, 3);                                                                                // L_: o "result"
    STACK_CHECK(L_, 1);
    lua_pushstring(L_, luaL_typename(L_, 1));                                                      // L_: o "result" "type"
    lua_replace(L_, -3);                                                                           // L_: "type" "result"
    return 2;
}
