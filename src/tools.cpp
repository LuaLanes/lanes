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
#include "_pch.hpp"

#include "tools.hpp"

#include "debugspew.hpp"
#include "universe.hpp"

DEBUGSPEW_CODE(std::string_view const DebugSpewIndentScope::debugspew_indent{ "----+----!----+----!----+----!----+----!----+----!----+----!----+----!----+" });

// xxh64 of string "kLookupCacheRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLookupCacheRegKey{ 0x9BF75F84E54B691Bull };

// #################################################################################################

namespace {
    namespace local {
        static int buf_writer([[maybe_unused]] lua_State* const L_, void const* b_, size_t size_, void* ud_)
        {
            auto* const _B{ static_cast<luaL_Buffer*>(ud_) };
            if (b_ && size_) {
                luaL_addlstring(_B, static_cast<char const*>(b_), size_);
            }
            return 0;
        }

        static constexpr int kWriterReturnCode{ 666 };
        [[nodiscard]]
        static int dummy_writer([[maybe_unused]] lua_State* const L_, [[maybe_unused]] void const* p_, [[maybe_unused]] size_t sz_, [[maybe_unused]] void* ud_)
        {
            // always fail with this code
            return kWriterReturnCode;
        }

    } // namespace local
} // namespace

// #################################################################################################

/*
 * differentiation between C, bytecode and JIT-fast functions
 *
 *                   +-------------------+------------+----------+
 *                   |      bytecode     | C function | JIT-fast |
 * +-----------------+-------------------+------------+----------+
 * | lua_tocfunction |      nullptr      |  <some p>  |  nullptr |
 * +-----------------+-------------------+------------+----------+
 * | luaW_dump       | kWriterReturnCode |  1         |  1       |
 * +-----------------+-------------------+------------+----------+
 */

[[nodiscard]]
FuncSubType luaW_getfuncsubtype(lua_State* const L_, StackIndex const i_)
{
    if (lua_tocfunction(L_, i_)) { // nullptr for LuaJIT-fast && bytecode functions
        return FuncSubType::Native;
    }

    // luaW_dump expects the function at the top of the stack
    int const _popCount{ (luaW_absindex(L_, i_) == lua_gettop(L_)) ? 0 : (lua_pushvalue(L_, i_), 1) };
    // here we either have a Lua bytecode or a LuaJIT-compiled function
    int const _dumpres{ luaW_dump(L_, local::dummy_writer, nullptr, 0) };
    if (_popCount > 0) {
        lua_pop(L_, _popCount);
    }
    if (_dumpres == local::kWriterReturnCode) {
        // anytime we get kWriterReturnCode, this means that luaW_dump() attempted a dump
        return FuncSubType::Bytecode;
    }
    // we didn't try to dump, therefore this is a LuaJIT-fast function
    return FuncSubType::FastJIT;
}

// #################################################################################################

namespace tools {
    // inspired from tconcat() in ltablib.c
    [[nodiscard]]
    std::string_view PushFQN(lua_State* const L_, StackIndex const t_)
    {
        STACK_CHECK_START_REL(L_, 0);
        // Lua 5.4 pushes &b as light userdata on the stack. be aware of it...
        luaL_Buffer _b;
        luaL_buffinit(L_, &_b);                                                                    // L_: ... {} ... &b?
        TableIndex _i{ 1 };
        TableIndex const _last{ static_cast<TableIndex::type>(lua_rawlen(L_, t_)) };
        for (; _i < _last; ++_i) {
            lua_rawgeti(L_, t_, _i);
            luaL_addvalue(&_b);
            luaL_addlstring(&_b, "/", 1);
        }
        if (_i == _last) { // add last value (if interval was not empty)
            lua_rawgeti(L_, t_, _i);
            luaL_addvalue(&_b);
        }
        // &b is popped at that point (-> replaced by the result)
        luaL_pushresult(&_b);                                                                      // L_: ... {} ... "<result>"
        STACK_CHECK(L_, 1);
        return luaW_tostring(L_, kIdxTop);
    }

    // #############################################################################################

    void PushFunctionBytecode(SourceState const L1_, DestState const L2_, int const strip_)
    {
        luaL_Buffer B{};
        STACK_CHECK_START_REL(L1_, 0);
        STACK_CHECK_START_REL(L2_, 0);
        STACK_GROW(L2_, 2);
        luaL_buffinit(L2_, &B);                                                                    // L1_: ... f                                     L2_: ... <B stuff>
        luaW_dump(L1_, local::buf_writer, &B, strip_);
        luaL_pushresult(&B);                                                                       //                                                L2_: ... "<bytecode>"
        STACK_CHECK(L2_, 1);
        STACK_CHECK(L1_, 0);
    }
} // namespace tools

// #################################################################################################

/*
 * receives 2 arguments: a name k and an object o
 * add two entries ["fully.qualified.name"] = o
 * and             [o] = "fully.qualified.name"
 * where <o> is either a table, a function, or a full userdata
 * if we already had an entry of type [o] = ..., replace the name if the new one is shorter
 * pops the processed object from the stack
 */
static void update_lookup_entry(lua_State* const L_, StackIndex const ctxBase_, TableIndex const depth_)
{
    // slot 1 in the stack contains the table that receives everything we found
    StackIndex const _dest{ ctxBase_ };
    // slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
    StackIndex const _fqn{ ctxBase_ + 1 };

    DEBUGSPEW_CODE(Universe* const _U{ Universe::Get(L_) });
    DEBUGSPEW_CODE(DebugSpew(_U) << "update_lookup_entry()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });

    STACK_CHECK_START_REL(L_, 0);
    // first, raise an error if the function is already known
    lua_pushvalue(L_, -1);                                                                         // L_: ... {bfc} k o o
    lua_rawget(L_, _dest);                                                                         // L_: ... {bfc} k o name?
    std::string_view const _prevName{ luaW_tostring(L_, kIdxTop) }; // nullptr if we got nil (first encounter of this object)
    // push name in fqn stack (note that concatenation will crash if name is a not string or a number)
    lua_pushvalue(L_, -3);                                                                         // L_: ... {bfc} k o name? k
    LUA_ASSERT(L_, luaW_type(L_, kIdxTop) == LuaType::NUMBER || luaW_type(L_, kIdxTop) == LuaType::STRING);
    TableIndex const _deeper{ depth_ + 1 };
    lua_rawseti(L_, _fqn, _deeper);                                                                // L_: ... {bfc} k o name?
    // generate name
    std::string_view const _newName{ tools::PushFQN(L_, _fqn) };                                   // L_: ... {bfc} k o name? "f.q.n"
    // Lua 5.2 introduced a hash randomizer seed which causes table iteration to yield a different key order
    // on different VMs even when the tables are populated the exact same way.
    // Also, when Lua is built with compatibility options (such as LUA_COMPAT_ALL), some base libraries register functions under multiple names.
    // This, with the randomizer, can cause the first generated name of an object to be different on different VMs, which breaks function transfer.
    // Also, nothing prevents any external module from exposing a given object under several names, so...
    // Therefore, when we encounter an object for which a name was previously registered, we need to select a single name
    // based on some sorting order so that we end up with the same name in all databases whatever order the table walk yielded
    if (!_prevName.empty() && ((_prevName.size() < _newName.size()) || (_prevName <= _newName))) {
        DEBUGSPEW_CODE(DebugSpew(_U) << luaW_typename(L_, StackIndex{ -3 }) << " '" << _newName << "' remains named '" << _prevName << "'" << std::endl);
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
        DEBUGSPEW_CODE(DebugSpew(_U) << luaW_typename(L_, StackIndex{ -2 }) << " '" << _newName << "'" << std::endl);
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
        lua_rawseti(L_, _fqn, _deeper);                                                            // L_: ... {bfc} k
    }
    STACK_CHECK(L_, -1);
}

// #################################################################################################

static void populate_lookup_table_recur(lua_State* const L_, StackIndex const dbIdx_, StackIndex const i_, TableIndex const depth_)
{
    // slot dbIdx_ contains the lookup database table
    // slot dbIdx_ + 1 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot i_
    StackIndex const _fqn{ dbIdx_ + 1 };
    // slot dbIdx_ + 2 contains a cache that stores all already visited tables to avoid infinite recursion loops
    StackIndex const _cache{ dbIdx_ + 2 };
    DEBUGSPEW_CODE(Universe* const _U{ Universe::Get(L_) });
    DEBUGSPEW_CODE(DebugSpew(_U) << "populate_lookup_table_recur()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });

    STACK_GROW(L_, 6);
    // slot i_ contains a table where we search for functions (or a full userdata with a metatable)
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: ... {i_}

    // if object is a userdata, replace it by its metatable
    if (luaW_type(L_, i_) == LuaType::USERDATA) {
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
    StackIndex const _breadthFirstCache{ lua_gettop(L_) };
    // iterate over all entries in the processed table
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    while (lua_next(L_, i_) != 0) {                                                                // L_: ... {i_} {bfc} k v
        // just for debug, not actually needed
        // std::string_view const _key{ (luaW_type(L_, -2) == LuaType::STRING) ? luaW_tostring(L_, -2) : "not a string" };
        // subtable: process it recursively
        if (lua_istable(L_, kIdxTop)) {                                                            // L_: ... {i_} {bfc} k {}
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
            lua_rawset(L_, _breadthFirstCache);                                                    // L_: ... {i_} {bfc} k {}
            // generate a name, and if we already had one name, keep whichever is the shorter
            update_lookup_entry(L_, dbIdx_, depth_);                                               // L_: ... {i_} {bfc} k
        } else if (lua_isfunction(L_, kIdxTop) && (luaW_getfuncsubtype(L_, kIdxTop) != FuncSubType::Bytecode)) {
            // generate a name, and if we already had one name, keep whichever is the shorter
            // this pops the function from the stack
            update_lookup_entry(L_, dbIdx_, depth_);                                               // L_: ... {i_} {bfc} k
        } else if (luaW_type(L_, kIdxTop) == LuaType::USERDATA) {
            // generate a name, and if we already had one name, keep whichever is the shorter
            // this pops the userdata from the stack
            update_lookup_entry(L_, dbIdx_, depth_);                                               // L_: ... {i_} {bfc} k
        } else {
            lua_pop(L_, 1);                                                                        // L_: ... {i_} {bfc} k
        }
        STACK_CHECK(L_, 2);
    }
    // now process the tables we encountered at that depth
    TableIndex const _deeper{ depth_ + 1 };
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    while (lua_next(L_, _breadthFirstCache) != 0) {                                                // L_: ... {i_} {bfc} k {}
        DEBUGSPEW_CODE(std::string_view const _key{ (luaW_type(L_, StackIndex{ -2 }) == LuaType::STRING) ? luaW_tostring(L_, StackIndex{ -2 }) : std::string_view{ "<not a string>" } });
        DEBUGSPEW_CODE(DebugSpew(_U) << "table '"<< _key <<"'" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope2{ _U });
        // un-visit this table in case we do need to process it
        lua_pushvalue(L_, -1);                                                                     // L_: ... {i_} {bfc} k {} {}
        lua_rawget(L_, _cache);                                                                    // L_: ... {i_} {bfc} k {} n
        LUA_ASSERT(L_, luaW_type(L_, kIdxTop) == LuaType::NUMBER);
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
        lua_rawseti(L_, _fqn, _deeper);                                                            // L_: ... {i_} {bfc} k {}
        populate_lookup_table_recur(L_, dbIdx_, StackIndex{ lua_gettop(L_) }, _deeper);
        lua_pop(L_, 1);                                                                            // L_: ... {i_} {bfc} k
        STACK_CHECK(L_, 2);
    }
    // remove table name from fqn stack
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    lua_rawseti(L_, _fqn, _deeper);                                                                // L_: ... {i_} {bfc}
    // we are done with our cache
    lua_pop(L_, 1);                                                                                // L_: ... {i_}
    STACK_CHECK(L_, 0);
    // we are done                                                                                 // L_: ... {i_} {bfc}
}

// #################################################################################################

namespace tools {

    // create a "fully.qualified.name" <-> function equivalence database
    void PopulateFuncLookupTable(lua_State* const L_, StackIndex const i_, std::string_view const& name_)
    {
        StackIndex const _in_base{ luaW_absindex(L_, i_) };
        DEBUGSPEW_CODE(Universe* _U = Universe::Get(L_));
        std::string_view _name{ name_.empty() ? std::string_view{} : name_ };
        DEBUGSPEW_CODE(DebugSpew(_U) << L_ << ": PopulateFuncLookupTable('" << _name << "')" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ _U });
        STACK_GROW(L_, 3);
        STACK_CHECK_START_REL(L_, 0);
        kLookupRegKey.pushValue(L_);                                                               // L_: {}
        StackIndex const _dbIdx{ lua_gettop(L_) };
        STACK_CHECK(L_, 1);
        LUA_ASSERT(L_, lua_istable(L_, -1));
        LuaType const _moduleType{ luaW_type(L_, _in_base) };
        if ((_moduleType == LuaType::FUNCTION) || (_moduleType == LuaType::USERDATA)) { // for example when a module is a simple function
            if (_name.empty()) {
                _name = "nullptr";
            }
            lua_pushvalue(L_, _in_base);                                                           // L_: {} f
            luaW_pushstring(L_, _name);                                                            // L_: {} f name_
            lua_rawset(L_, -3);                                                                    // L_: {}
            luaW_pushstring(L_, _name);                                                            // L_: {} name_
            lua_pushvalue(L_, _in_base);                                                           // L_: {} name_ f
            lua_rawset(L_, -3);                                                                    // L_: {}
            lua_pop(L_, 1);                                                                        // L_:
        } else if (luaW_type(L_, _in_base) == LuaType::TABLE) {
            lua_newtable(L_);                                                                      // L_: {} {fqn}
            TableIndex _startDepth{ 0 };
            if (!_name.empty()) {
                STACK_CHECK(L_, 2);
                luaW_pushstring(L_, _name);                                                        // L_: {} {fqn} "name"
                // generate a name, and if we already had one name, keep whichever is the shorter
                lua_pushvalue(L_, _in_base);                                                       // L_: {} {fqn} "name" t
                update_lookup_entry(L_, _dbIdx, _startDepth);                                      // L_: {} {fqn} "name"
                // don't forget to store the name at the bottom of the fqn stack
                lua_rawseti(L_, -2, ++_startDepth);                                                // L_: {} {fqn}
                STACK_CHECK(L_, 2);
            }
            // retrieve the cache, create it if we haven't done it yet
            std::ignore = kLookupCacheRegKey.getSubTable(L_, NArr{ 0 }, NRec{ 0 });                // L_: {} {fqn} {cache}
            // process everything we find in that table, filling in lookup data for all functions and tables we see there
            populate_lookup_table_recur(L_, _dbIdx, _in_base, _startDepth);
            lua_pop(L_, 3);                                                                        // L_:
        } else {
            lua_pop(L_, 1);                                                                        // L_:
            raise_luaL_error(L_, "unsupported module type %s", luaW_typename(L_, _in_base).data());
        }
        STACK_CHECK(L_, 0);
    }

} // namespace tools

// #################################################################################################

namespace tools {

    // Serialize calls to 'require', if it exists
    void SerializeRequire(lua_State* L_)
    {
        static constexpr lua_CFunction _lockedRequire{
            +[](lua_State* L_)
            {
                int const _args{ lua_gettop(L_) };                                                 // L_: args...
                //[[maybe_unused]] std::string_view const _modname{ luaW_checkstring(L_, 1) };

                STACK_GROW(L_, 1);

                lua_pushvalue(L_, lua_upvalueindex(1));                                            // L_: args... require
                lua_insert(L_, 1);                                                                 // L_: require args...

                // Using 'lua_pcall()' to catch errors; otherwise a failing 'require' would
                // leave us locked, blocking any future 'require' calls from other lanes.
                LuaError const _rc{ std::invoke(
                    [L = L_, args = _args]()
                    {
                        std::lock_guard _guard{ Universe::Get(L)->requireMutex };
                        // starting with Lua 5.4, require may return a second optional value, so we need LUA_MULTRET
                        return lua_pcall(L, args, LUA_MULTRET, 0 /*errfunc*/);                     // L_: err|result(s)
                    })
                };

                // the required module (or an error message) is left on the stack as returned value by original require function

                if (_rc != LuaError::OK) { // LUA_ERRRUN / LUA_ERRMEM ?
                    raise_lua_error(L_);
                }
                // should be 1 for Lua <= 5.3, 1 or 2 starting with Lua 5.4
                return lua_gettop(L_);                                                             // L_: result(s)
            }
        };

        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);
        DEBUGSPEW_CODE(DebugSpew(Universe::Get(L_)) << "serializing require()" << std::endl);

        // Check 'require' is there and not already wrapped; if not, do nothing
        lua_getglobal(L_, "require");                                                              // L_: _G.require()|nil
        if (lua_isfunction(L_, -1) && lua_tocfunction(L_, -1) != _lockedRequire) {
            lua_pushcclosure(L_, _lockedRequire, 1 /*upvalues*/);                                  // L_: _lockedRequire()
            lua_setglobal(L_, "require");                                                          // L_:
        } else {
            lua_pop(L_, 1);                                                                        // L_:
        }

        STACK_CHECK(L_, 0);
    }
} // namespace tools

