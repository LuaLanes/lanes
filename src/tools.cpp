/*
 * TOOLS.C                         Copyright (c) 2002-10, Asko Kauppi
 *
 * Lua tools to support Lanes.
 */

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
              2011-17 benoit Germain <bnt.germain@gmail.com>

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

#include "universe.h"

DEBUGSPEW_CODE(char const* const DebugSpewIndentScope::debugspew_indent = "----+----!----+----!----+----!----+----!----+----!----+----!----+----!----+");

// xxh64 of string "kLookupCacheRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLookupCacheRegKey{ 0x9BF75F84E54B691Bull };

// #################################################################################################

// same as PUC-Lua l_alloc
extern "C" [[nodiscard]] static void* libc_lua_Alloc([[maybe_unused]] void* ud, [[maybe_unused]] void* ptr_, [[maybe_unused]] size_t osize_, size_t nsize_)
{
    if (nsize_ == 0) {
        free(ptr_);
        return nullptr;
    } else {
        return realloc(ptr_, nsize_);
    }
}

// #################################################################################################

[[nodiscard]] static int luaG_provide_protected_allocator(lua_State* L_)
{
    Universe* const U{ universe_get(L_) };
    // push a new full userdata on the stack, giving access to the universe's protected allocator
    [[maybe_unused]] AllocatorDefinition* const def{ new (L_) AllocatorDefinition{ U->protectedAllocator.makeDefinition() } };
    return 1;
}

// #################################################################################################

// called once at the creation of the universe (therefore L is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
void initialize_allocator_function(Universe* U_, lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    lua_getfield(L_, -1, "allocator");                                                             // L_: settings allocator|nil|"protected"
    if (!lua_isnil(L_, -1)) {
        // store C function pointer in an internal variable
        U_->provideAllocator = lua_tocfunction(L_, -1);                                            // L_: settings allocator
        if (U_->provideAllocator != nullptr) {
            // make sure the function doesn't have upvalues
            char const* upname = lua_getupvalue(L_, -1, 1);                                        // L_: settings allocator upval?
            if (upname != nullptr) {   // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "config.allocator() shouldn't have upvalues");
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings allocator nil
            lua_setfield(L_, -3, "allocator");                                                     // L_: settings allocator
        } else if (lua_type(L_, -1) == LUA_TSTRING) { // should be "protected"
            LUA_ASSERT(L_, strcmp(lua_tostring(L_, -1), "protected") == 0);
            // set the original allocator to call from inside protection by the mutex
            U_->protectedAllocator.initFrom(L_);
            U_->protectedAllocator.installIn(L_);
            // before a state is created, this function will be called to obtain the allocator
            U_->provideAllocator = luaG_provide_protected_allocator;
        }
    } else {
        // just grab whatever allocator was provided to lua_newstate
        U_->protectedAllocator.initFrom(L_);
    }
    lua_pop(L_, 1); // L_: settings
    STACK_CHECK(L_, 1);

    lua_getfield(L_, -1, "internal_allocator");                                                    // L_: settings "libc"|"allocator"
    {
        char const* allocator = lua_tostring(L_, -1);
        if (strcmp(allocator, "libc") == 0) {
            U_->internalAllocator = AllocatorDefinition{ libc_lua_Alloc, nullptr };
        } else if (U_->provideAllocator == luaG_provide_protected_allocator) {
            // user wants mutex protection on the state's allocator. Use protection for our own allocations too, just in case.
            U_->internalAllocator = U_->protectedAllocator.makeDefinition();
        } else {
            // no protection required, just use whatever we have as-is.
            U_->internalAllocator = U_->protectedAllocator;
        }
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);
}

// #################################################################################################

[[nodiscard]] static int dummy_writer([[maybe_unused]] lua_State* L_, [[maybe_unused]] void const* p_, [[maybe_unused]] size_t sz_, [[maybe_unused]] void* ud_)
{
    return 666;
}

/*
 * differentiation between C, bytecode and JIT-fast functions
 *
 *
 *                   +----------+------------+----------+
 *                   | bytecode | C function | JIT-fast |
 * +-----------------+----------+------------+----------+
 * | lua_topointer   |          |            |          |
 * +-----------------+----------+------------+----------+
 * | lua_tocfunction |  nullptr |            |  nullptr |
 * +-----------------+----------+------------+----------+
 * | lua_dump        |  666     |  1         |  1       |
 * +-----------------+----------+------------+----------+
 */

enum class FuncSubType
{
    Bytecode,
    Native,
    FastJIT
};

FuncSubType luaG_getfuncsubtype(lua_State* L_, int _i)
{
    if (lua_tocfunction(L_, _i)) { // nullptr for LuaJIT-fast && bytecode functions
        return FuncSubType::Native;
    }
    {
        int mustpush{ 0 };
        if (lua_absindex(L_, _i) != lua_gettop(L_)) {
            lua_pushvalue(L_, _i);
            mustpush = 1;
        }
        // the provided writer fails with code 666
        // therefore, anytime we get 666, this means that lua_dump() attempted a dump
        // all other cases mean this is either a C or LuaJIT-fast function
        int const dumpres{ lua504_dump(L_, dummy_writer, nullptr, 0) };
        lua_pop(L_, mustpush);
        if (dumpres == 666) {
            return FuncSubType::Bytecode;
        }
    }
    return FuncSubType::FastJIT;
}

// #################################################################################################

// inspired from tconcat() in ltablib.c
[[nodiscard]] static char const* luaG_pushFQN(lua_State* L_, int t_, int last_, size_t* length_)
{
    luaL_Buffer b;
    STACK_CHECK_START_REL(L_, 0);
    // Lua 5.4 pushes &b as light userdata on the stack. be aware of it...
    luaL_buffinit(L_, &b);                                                                         // L_: ... {} ... &b?
    int i = 1;
    for (; i < last_; ++i) {
        lua_rawgeti(L_, t_, i);
        luaL_addvalue(&b);
        luaL_addlstring(&b, "/", 1);
    }
    if (i == last_) { // add last value (if interval was not empty)
        lua_rawgeti(L_, t_, i);
        luaL_addvalue(&b);
    }
    // &b is popped at that point (-> replaced by the result)
    luaL_pushresult(&b);                                                                           // L_: ... {} ... "<result>"
    STACK_CHECK(L_, 1);
    return lua_tolstring(L_, -1, length_);
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
static void update_lookup_entry(DEBUGSPEW_PARAM_COMMA(Universe* U_) lua_State* L_, int ctxBase_, int depth_)
{
    // slot 1 in the stack contains the table that receives everything we found
    int const dest{ ctxBase_ };
    // slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
    int const fqn{ ctxBase_ + 1 };

    size_t prevNameLength, newNameLength;
    char const* prevName;
    DEBUGSPEW_CODE(char const* newName);
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "update_lookup_entry()\n" INDENT_END(U_)));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U_ });

    STACK_CHECK_START_REL(L_, 0);
    // first, raise an error if the function is already known
    lua_pushvalue(L_, -1);                                                                         // L_: ... {bfc} k o o
    lua_rawget(L_, dest);                                                                          // L_: ... {bfc} k o name?
    prevName = lua_tolstring(L_, -1, &prevNameLength); // nullptr if we got nil (first encounter of this object)
    // push name in fqn stack (note that concatenation will crash if name is a not string or a number)
    lua_pushvalue(L_, -3);                                                                         // L_: ... {bfc} k o name? k
    LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TNUMBER || lua_type(L_, -1) == LUA_TSTRING);
    ++depth_;
    lua_rawseti(L_, fqn, depth_);                                                                  // L_: ... {bfc} k o name?
    // generate name
    DEBUGSPEW_OR_NOT(newName, std::ignore) = luaG_pushFQN(L_, fqn, depth_, &newNameLength);        // L_: ... {bfc} k o name? "f.q.n"
    // Lua 5.2 introduced a hash randomizer seed which causes table iteration to yield a different key order
    // on different VMs even when the tables are populated the exact same way.
    // When Lua is built with compatibility options (such as LUA_COMPAT_ALL),
    // this causes several base libraries to register functions under multiple names.
    // This, with the randomizer, can cause the first generated name of an object to be different on different VMs,
    // which breaks function transfer.
    // Also, nothing prevents any external module from exposing a given object under several names, so...
    // Therefore, when we encounter an object for which a name was previously registered, we need to select the names
    // based on some sorting order so that we end up with the same name in all databases whatever order the table walk yielded
    if (prevName != nullptr && (prevNameLength < newNameLength || lua_lessthan(L_, -2, -1))) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%s '%s' remained named '%s'\n" INDENT_END(U_), lua_typename(L_, lua_type(L_, -3)), newName, prevName));
        // the previous name is 'smaller' than the one we just generated: keep it!
        lua_pop(L_, 3);                                                                            // L_: ... {bfc} k
    } else {
        // the name we generated is either the first one, or a better fit for our purposes
        if (prevName) {
            // clear the previous name for the database to avoid clutter
            lua_insert(L_, -2);                                                                    // L_: ... {bfc} k o "f.q.n" prevName
            // t[prevName] = nil
            lua_pushnil(L_);                                                                       // L_: ... {bfc} k o "f.q.n" prevName nil
            lua_rawset(L_, dest);                                                                  // L_: ... {bfc} k o "f.q.n"
        } else {
            lua_remove(L_, -2);                                                                    // L_: ... {bfc} k o "f.q.n"
        }
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%s '%s'\n" INDENT_END(U_), lua_typename(L_, lua_type(L_, -2)), newName));
        // prepare the stack for database feed
        lua_pushvalue(L_, -1);                                                                     // L_: ... {bfc} k o "f.q.n" "f.q.n"
        lua_pushvalue(L_, -3);                                                                     // L_: ... {bfc} k o "f.q.n" "f.q.n" o
        LUA_ASSERT(L_, lua_rawequal(L_, -1, -4));
        LUA_ASSERT(L_, lua_rawequal(L_, -2, -3));
        // t["f.q.n"] = o
        lua_rawset(L_, dest);                                                                      // L_: ... {bfc} k o "f.q.n"
        // t[o] = "f.q.n"
        lua_rawset(L_, dest);                                                                      // L_: ... {bfc} k
        // remove table name from fqn stack
        lua_pushnil(L_);                                                                           // L_: ... {bfc} k nil
        lua_rawseti(L_, fqn, depth_);                                                              // L_: ... {bfc} k
    }
    --depth_;
    STACK_CHECK(L_, -1);
}

// #################################################################################################

static void populate_func_lookup_table_recur(DEBUGSPEW_PARAM_COMMA(Universe* U_) lua_State* L_, int dbIdx_, int i_, int depth_)
{
    // slot dbIdx_ contains the lookup database table
    // slot dbIdx_ + 1 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot i_
    int const fqn{ dbIdx_ + 1 };
    // slot dbIdx_ + 2 contains a cache that stores all already visited tables to avoid infinite recursion loops
    int const cache{ dbIdx_ + 2 };
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "populate_func_lookup_table_recur()\n" INDENT_END(U_)));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U_ });

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
    lua_rawget(L_, cache);                                                                         // L_: ... {i_} nil|n
    lua_Integer visit_count{ lua_tointeger(L_, -1) }; // 0 if nil, else n
    lua_pop(L_, 1);                                                                                // L_: ... {i_}
    STACK_CHECK(L_, 0);
    if (visit_count > 0) {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "already visited\n" INDENT_END(U_)));
        return;
    }

    // remember we visited this table (1-visit count)
    lua_pushvalue(L_, i_);                                                                         // L_: ... {i_} {}
    lua_pushinteger(L_, visit_count + 1);                                                          // L_: ... {i_} {} 1
    lua_rawset(L_, cache);                                                                         // L_: ... {i_}
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
            lua_rawget(L_, cache);                                                                 // L_: ... {i_} {bfc} k {} {} n?
            visit_count = lua_tointeger(L_, -1) + 1; // 1 if we got nil, else n+1
            lua_pop(L_, 1);                                                                        // L_: ... {i_} {bfc} k {} {}
            lua_pushinteger(L_, visit_count);                                                      // L_: ... {i_} {bfc} k {} {} n
            lua_rawset(L_, cache);                                                                 // L_: ... {i_} {bfc} k {}
            // store the table in the breadth-first cache
            lua_pushvalue(L_, -2);                                                                 // L_: ... {i_} {bfc} k {} k
            lua_pushvalue(L_, -2);                                                                 // L_: ... {i_} {bfc} k {} k {}
            lua_rawset(L_, breadthFirstCache);                                                     // L_: ... {i_} {bfc} k {}
            // generate a name, and if we already had one name, keep whichever is the shorter
            update_lookup_entry(DEBUGSPEW_PARAM_COMMA(U_) L_, dbIdx_, depth_);                     // L_: ... {i_} {bfc} k
        } else if (lua_isfunction(L_, -1) && (luaG_getfuncsubtype(L_, -1) != FuncSubType::Bytecode)) {
            // generate a name, and if we already had one name, keep whichever is the shorter
            // this pops the function from the stack
            update_lookup_entry(DEBUGSPEW_PARAM_COMMA(U_) L_, dbIdx_, depth_);                     // L_: ... {i_} {bfc} k
        } else {
            lua_pop(L_, 1); // L_: ... {i_} {bfc} k
        }
        STACK_CHECK(L_, 2);
    }
    // now process the tables we encountered at that depth
    ++depth_;
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    while (lua_next(L_, breadthFirstCache) != 0) {                                                 // L_: ... {i_} {bfc} k {}
        DEBUGSPEW_CODE(char const* key = (lua_type(L_, -2) == LUA_TSTRING) ? lua_tostring(L_, -2) : "not a string");
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "table '%s'\n" INDENT_END(U_), key));
        DEBUGSPEW_CODE(DebugSpewIndentScope scope2{ U_ });
        // un-visit this table in case we do need to process it
        lua_pushvalue(L_, -1);                                                                     // L_: ... {i_} {bfc} k {} {}
        lua_rawget(L_, cache);                                                                     // L_: ... {i_} {bfc} k {} n
        LUA_ASSERT(L_, lua_type(L_, -1) == LUA_TNUMBER);
        visit_count = lua_tointeger(L_, -1) - 1;
        lua_pop(L_, 1);                                                                            // L_: ... {i_} {bfc} k {}
        lua_pushvalue(L_, -1);                                                                     // L_: ... {i_} {bfc} k {} {}
        if (visit_count > 0) {
            lua_pushinteger(L_, visit_count);                                                      // L_: ... {i_} {bfc} k {} {} n
        } else {
            lua_pushnil(L_);                                                                       // L_: ... {i_} {bfc} k {} {} nil
        }
        lua_rawset(L_, cache);                                                                     // L_: ... {i_} {bfc} k {}
        // push table name in fqn stack (note that concatenation will crash if name is a not string!)
        lua_pushvalue(L_, -2);                                                                     // L_: ... {i_} {bfc} k {} k
        lua_rawseti(L_, fqn, depth_);                                                              // L_: ... {i_} {bfc} k {}
        populate_func_lookup_table_recur(DEBUGSPEW_PARAM_COMMA(U_) L_, dbIdx_, lua_gettop(L_), depth_);
        lua_pop(L_, 1);                                                                            // L_: ... {i_} {bfc} k
        STACK_CHECK(L_, 2);
    }
    // remove table name from fqn stack
    lua_pushnil(L_);                                                                               // L_: ... {i_} {bfc} nil
    lua_rawseti(L_, fqn, depth_);                                                                  // L_: ... {i_} {bfc}
    --depth_;
    // we are done with our cache
    lua_pop(L_, 1);                                                                                // L_: ... {i_}
    STACK_CHECK(L_, 0);
    // we are done                                                                                 // L_: ... {i_} {bfc}
}

// #################################################################################################

// create a "fully.qualified.name" <-> function equivalence database
void populate_func_lookup_table(lua_State* L_, int i_, char const* name_)
{
    int const in_base = lua_absindex(L_, i_);
    DEBUGSPEW_CODE(Universe* U = universe_get(L_));
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%p: populate_func_lookup_table('%s')\n" INDENT_END(U), L_, name_ ? name_ : "nullptr"));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);
    kLookupRegKey.pushValue(L_);                                                                   // L_: {}
    int const dbIdx{ lua_gettop(L_) };
    STACK_CHECK(L_, 1);
    LUA_ASSERT(L_, lua_istable(L_, -1));
    if (lua_type(L_, in_base) == LUA_TFUNCTION) { // for example when a module is a simple function
        name_ = name_ ? name_ : "nullptr";
        lua_pushvalue(L_, in_base);                                                                // L_: {} f
        lua_pushstring(L_, name_);                                                                 // L_: {} f _name
        lua_rawset(L_, -3);                                                                        // L_: {}
        lua_pushstring(L_, name_);                                                                 // L_: {} _name
        lua_pushvalue(L_, in_base);                                                                // L_: {} _name f
        lua_rawset(L_, -3);                                                                        // L_: {}
        lua_pop(L_, 1);                                                                            // L_:
    } else if (lua_type(L_, in_base) == LUA_TTABLE) {
        lua_newtable(L_);                                                                          // L_: {} {fqn}
        int startDepth{ 0 };
        if (name_) {
            STACK_CHECK(L_, 2);
            lua_pushstring(L_, name_);                                                             // L_: {} {fqn} "name"
            // generate a name, and if we already had one name, keep whichever is the shorter
            lua_pushvalue(L_, in_base);                                                            // L_: {} {fqn} "name" t
            update_lookup_entry(DEBUGSPEW_PARAM_COMMA(U) L_, dbIdx, startDepth);                   // L_: {} {fqn} "name"
            // don't forget to store the name at the bottom of the fqn stack
            lua_rawseti(L_, -2, ++startDepth);                                                     // L_: {} {fqn}
            STACK_CHECK(L_, 2);
        }
        // retrieve the cache, create it if we haven't done it yet
        std::ignore = kLookupCacheRegKey.getSubTable(L_, 0, 0);                                    // L_: {} {fqn} {cache}
        // process everything we find in that table, filling in lookup data for all functions and tables we see there
        populate_func_lookup_table_recur(DEBUGSPEW_PARAM_COMMA(U) L_, dbIdx, in_base, startDepth);
        lua_pop(L_, 3);                                                                            // L_:
    } else {
        lua_pop(L_, 1);                                                                            // L_:
        raise_luaL_error(L_, "unsupported module type %s", lua_typename(L_, lua_type(L_, in_base)));
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

/*---=== Inter-state copying ===---*/

// xxh64 of string "kMtIdRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kMtIdRegKey{ 0xA8895DCF4EC3FE3Cull };

// get a unique ID for metatable at [i].
[[nodiscard]] static lua_Integer get_mt_id(Universe* U_, lua_State* L_, int idx_)
{
    idx_ = lua_absindex(L_, idx_);

    STACK_GROW(L_, 3);

    STACK_CHECK_START_REL(L_, 0);
    std::ignore = kMtIdRegKey.getSubTable(L_, 0, 0);                                               // L_: ... _R[kMtIdRegKey]
    lua_pushvalue(L_, idx_);                                                                       // L_: ... _R[kMtIdRegKey] {mt}
    lua_rawget(L_, -2);                                                                            // L_: ... _R[kMtIdRegKey] mtk?

    lua_Integer id{ lua_tointeger(L_, -1) }; // 0 for nil
    lua_pop(L_, 1);                                                                                // L_: ... _R[kMtIdRegKey]
    STACK_CHECK(L_, 1);

    if (id == 0) {
        id = U_->nextMetatableId.fetch_add(1, std::memory_order_relaxed);

        // Create two-way references: id_uint <-> table
        lua_pushvalue(L_, idx_);                                                                   // L_: ... _R[kMtIdRegKey] {mt}
        lua_pushinteger(L_, id);                                                                   // L_: ... _R[kMtIdRegKey] {mt} id
        lua_rawset(L_, -3);                                                                        // L_: ... _R[kMtIdRegKey]

        lua_pushinteger(L_, id);                                                                   // L_: ... _R[kMtIdRegKey] id
        lua_pushvalue(L_, idx_);                                                                   // L_: ... _R[kMtIdRegKey] id {mt}
        lua_rawset(L_, -3);                                                                        // L_: ... _R[kMtIdRegKey]
    }
    lua_pop(L_, 1);                                                                                // L_: ...
    STACK_CHECK(L_, 0);

    return id;
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
[[nodiscard]] static char const* find_lookup_name(lua_State* L_, int i_, LookupMode mode_, char const* upName_, size_t* len_)
{
    LUA_ASSERT(L_, lua_isfunction(L_, i_) || lua_istable(L_, i_));                                 // L_: ... v ...
    STACK_CHECK_START_REL(L_, 0);
    STACK_GROW(L_, 3); // up to 3 slots are necessary on error
    if (mode_ == LookupMode::FromKeeper) {
        lua_CFunction f = lua_tocfunction(L_, i_); // should *always* be one of the function sentinels
        if (f == func_lookup_sentinel || f == table_lookup_sentinel || f == userdata_clone_sentinel) {
            lua_getupvalue(L_, i_, 1);                                                             // L_: ... v ... "f.q.n"
        } else {
            // if this is not a sentinel, this is some user-created table we wanted to lookup
            LUA_ASSERT(L_, nullptr == f && lua_istable(L_, i_));
            // push anything that will convert to nullptr string
            lua_pushnil(L_);                                                                       // L_: ... v ... nil
        }
    } else {
        // fetch the name from the source state's lookup table
        kLookupRegKey.pushValue(L_);                                                               // L_: ... v ... {}
        STACK_CHECK(L_, 1);
        LUA_ASSERT(L_, lua_istable(L_, -1));
        lua_pushvalue(L_, i_);                                                                     // L_: ... v ... {} v
        lua_rawget(L_, -2);                                                                        // L_: ... v ... {} "f.q.n"
    }
    char const* fqn{ lua_tolstring(L_, -1, len_) };
    DEBUGSPEW_CODE(Universe* const U = universe_get(L_));
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "function [C] %s \n" INDENT_END(U), fqn));
    // popping doesn't invalidate the pointer since this is an interned string gotten from the lookup database
    lua_pop(L_, (mode_ == LookupMode::FromKeeper) ? 1 : 2);                                        // L_: ... v ...
    STACK_CHECK(L_, 0);
    if (nullptr == fqn && !lua_istable(L_, i_)) { // raise an error if we try to send an unknown function (but not for tables)
        *len_ = 0; // just in case
        // try to discover the name of the function we want to send
        lua_getglobal(L_, "decoda_name");                                                          // L_: ... v ... decoda_name
        char const* from{ lua_tostring(L_, -1) };
        lua_pushcfunction(L_, luaG_nameof);                                                        // L_: ... v ... decoda_name luaG_nameof
        lua_pushvalue(L_, i_);                                                                     // L_: ... v ... decoda_name luaG_nameof t
        lua_call(L_, 1, 2);                                                                        // L_: ... v ... decoda_name "type" "name"|nil
        char const* typewhat{ (lua_type(L_, -2) == LUA_TSTRING) ? lua_tostring(L_, -2) : luaL_typename(L_, -2) };
        // second return value can be nil if the table was not found
        // probable reason: the function was removed from the source Lua state before Lanes was required.
        char const *what, *gotchaA, *gotchaB;
        if (lua_isnil(L_, -1)) {
            gotchaA = " referenced by";
            gotchaB = "\n(did you remove it from the source Lua state before requiring Lanes?)";
            what = upName_;
        } else {
            gotchaA = "";
            gotchaB = "";
            what = (lua_type(L_, -1) == LUA_TSTRING) ? lua_tostring(L_, -1) : luaL_typename(L_, -1);
        }
        raise_luaL_error(L_, "%s%s '%s' not found in %s origin transfer database.%s", typewhat, gotchaA, what, from ? from : "main", gotchaB);
    }
    STACK_CHECK(L_, 0);
    return fqn;
}

// #################################################################################################

// Push a looked-up table, or nothing if we found nothing
[[nodiscard]] bool InterCopyContext::lookup_table() const
{
    // get the name of the table we want to send
    size_t len;
    char const* fqn = find_lookup_name(L1, L1_i, mode, name, &len);
    if (nullptr == fqn) { // name not found, it is some user-created table
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
        lua_pushlstring(L2, fqn, len);                                                             // L1: ... t ...                                  L2: "f.q.n"
        lua_pushcclosure(L2, table_lookup_sentinel, 1);                                            // L1: ... t ...                                  L2: f
        break;

    case LookupMode::LaneBody:
    case LookupMode::FromKeeper:
        kLookupRegKey.pushValue(L2);                                                               // L1: ... t ...                                  L2: {}
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_istable(L2, -1));
        lua_pushlstring(L2, fqn, len); // L2: {} "f.q.n"
        lua_rawget(L2, -2);            // L2: {} t
        // we accept destination lookup failures in the case of transfering the Lanes body function (this will result in the source table being cloned instead)
        // but not when we extract something out of a keeper, as there is nothing to clone!
        if (lua_isnil(L2, -1) && mode == LookupMode::LaneBody) {
            lua_pop(L2, 2);                                                                        // L1: ... t ...                                   L2:
            STACK_CHECK(L2, 0);
            return false;
        } else if (!lua_istable(L2, -1)) { // this can happen if someone decides to replace same already registered item (for a example a standard lib function) with a table
            lua_getglobal(L1, "decoda_name");                                                      // L1: ... t ... decoda_name
            char const* from{ lua_tostring(L1, -1) };
            lua_pop(L1, 1);                                                                        // L1: ... t ...
            lua_getglobal(L2, "decoda_name");                                                      // L1: ... t ...                                  L2: {} t decoda_name
            char const* to{ lua_tostring(L2, -1) };
            lua_pop(L2, 1);                                                                        // L1: ... t ...                                  L2: {} t
            raise_luaL_error(
                getErrL(),
                "%s: source table '%s' found as %s in %s destination transfer database.",
                from ? from : "main",
                fqn,
                lua_typename(L2, lua_type_as_enum(L2, -1)),
                to ? to : "main"
            );
        }
        lua_remove(L2, -2);                                                                        // L1: ... t ...                                  L2: t
        break;
    }
    STACK_CHECK(L2, 1);
    return true;
}

// #################################################################################################

// Check if we've already copied the same table from 'L1', and reuse the old copy. This allows table upvalues shared by multiple
// local functions to point to the same table, also in the target.
// Always pushes a table to 'L2'.
// Returns true if the table was cached (no need to fill it!); false if it's a virgin.
[[nodiscard]] bool InterCopyContext::push_cached_table() const
{
    void const* p{ lua_topointer(L1, L1_i) };

    LUA_ASSERT(L1, L2_cache_i != 0);
    STACK_GROW(L2, 3);
    STACK_CHECK_START_REL(L2, 0);

    // We don't need to use the from state ('L1') in ID since the life span
    // is only for the duration of a copy (both states are locked).
    // push a light userdata uniquely representing the table
    lua_pushlightuserdata(L2, const_cast<void*>(p));                                               // L1: ... t ...                                  L2: ... p

    // fprintf(stderr, "<< ID: %s >>\n", lua_tostring(L2, -1));

    lua_rawget(L2, L2_cache_i);                                                                    // L1: ... t ...                                  L2: ... {cached|nil}
    bool const not_found_in_cache{ lua_isnil(L2, -1) };
    if (not_found_in_cache) {
        // create a new entry in the cache
        lua_pop(L2, 1);                                                                            // L1: ... t ...                                  L2: ...
        lua_newtable(L2);                                                                          // L1: ... t ...                                  L2: ... {}
        lua_pushlightuserdata(L2, const_cast<void*>(p));                                           // L1: ... t ...                                  L2: ... {} p
        lua_pushvalue(L2, -2);                                                                     // L1: ... t ...                                  L2: ... {} p {}
        lua_rawset(L2, L2_cache_i);                                                                // L1: ... t ...                                  L2: ... {}
    }
    STACK_CHECK(L2, 1);
    LUA_ASSERT(L1, lua_istable(L2, -1));
    return !not_found_in_cache;
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
                std::ignore = luaG_pushFQN(L_, kFQN, depth_, nullptr);                             // L_: o "r" {c} {fqn} ... {?} k v "fqn"
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
                lua_pop(L_, 1); // L_: o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L_, 2);
            // search in the object's uservalues
            {
                int uvi = 1;
                while (lua_getiuservalue(L_, -1, uvi) != LUA_TNONE) {                              // L_: o "r" {c} {fqn} ... {?} k U {u}
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
                    ++uvi;
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
    int const what{ lua_gettop(L_) };
    if (what > 1) {
        raise_luaL_argerror(L_, what, "too many arguments.");
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
    lua_pushliteral(L_, "_G");                                                                     // L_: o nil {c} {fqn} "_G"
    lua_rawseti(L_, -2, 1);                                                                        // L_: o nil {c} {fqn}
    // this is where we start the search
    lua_pushglobaltable(L_);                                                                       // L_: o nil {c} {fqn} _G
    std::ignore = DiscoverObjectNameRecur(L_, 6666, 1);
    if (lua_isnil(L_, 2)) { // try again with registry, just in case...
        lua_pop(L_, 1);                                                                            // L_: o nil {c} {fqn}
        lua_pushliteral(L_, "_R");                                                                 // L_: o nil {c} {fqn} "_R"
        lua_rawseti(L_, -2, 1);                                                                    // L_: o nil {c} {fqn}
        lua_pushvalue(L_, LUA_REGISTRYINDEX);                                                      // L_: o nil {c} {fqn} _R
        std::ignore = DiscoverObjectNameRecur(L_, 6666, 1);
    }
    lua_pop(L_, 3);                                                                                // L_: o "result"
    STACK_CHECK(L_, 1);
    lua_pushstring(L_, luaL_typename(L_, 1));                                                      // L_: o "result" "type"
    lua_replace(L_, -3);                                                                           // L_: "type" "result"
    return 2;
}

// #################################################################################################

// Push a looked-up native/LuaJIT function.
void InterCopyContext::lookup_native_func() const
{
    // get the name of the function we want to send
    size_t len;
    char const* const fqn{ find_lookup_name(L1, L1_i, mode, name, &len) };
    // push the equivalent function in the destination's stack, retrieved from the lookup table
    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 3); // up to 3 slots are necessary on error
    switch (mode) {
    default: // shouldn't happen, in theory...
        raise_luaL_error(getErrL(), "internal error: unknown lookup mode");
        break;

    case LookupMode::ToKeeper:
        // push a sentinel closure that holds the lookup name as upvalue
        lua_pushlstring(L2, fqn, len);                                                             // L1: ... f ...                                  L2: "f.q.n"
        lua_pushcclosure(L2, func_lookup_sentinel, 1);                                             // L1: ... f ...                                  L2: f
        break;

    case LookupMode::LaneBody:
    case LookupMode::FromKeeper:
        kLookupRegKey.pushValue(L2);                                                               // L1: ... f ...                                  L2: {}
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_istable(L2, -1));
        lua_pushlstring(L2, fqn, len);                                                             // L1: ... f ...                                  L2: {} "f.q.n"
        lua_rawget(L2, -2);                                                                        // L1: ... f ...                                  L2: {} f
        // nil means we don't know how to transfer stuff: user should do something
        // anything other than function or table should not happen!
        if (!lua_isfunction(L2, -1) && !lua_istable(L2, -1)) {
            lua_getglobal(L1, "decoda_name");                                                      // L1: ... f ... decoda_name
            char const* const from{ lua_tostring(L1, -1) };
            lua_pop(L1, 1);                                                                        // L1: ... f ...
            lua_getglobal(L2, "decoda_name");                                                      // L1: ... f ...                                  L2: {} f decoda_name
            char const* const to{ lua_tostring(L2, -1) };
            lua_pop(L2, 1); // L2: {} f
            // when mode_ == LookupMode::FromKeeper, L is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
            raise_luaL_error(
                getErrL()
                , "%s%s: function '%s' not found in %s destination transfer database."
                , lua_isnil(L2, -1) ? "" : "INTERNAL ERROR IN "
                , from ? from : "main"
                , fqn
                , to ? to : "main"
            );
            return;
        }
        lua_remove(L2, -2); // L2: f
        break;

    /* keep it in case I need it someday, who knows...
    case LookupMode::RawFunctions:
    {
        int n;
        char const* upname;
        lua_CFunction f = lua_tocfunction( L, i);
        // copy upvalues
        for (n = 0; (upname = lua_getupvalue( L, i, 1 + n)) != nullptr; ++ n) {
            luaG_inter_move( U, L, L2, 1, mode_);                                                  //                                                L2: [up[,up ...]]
        }
        lua_pushcclosure( L2, f, n);                                                               //                                                L2:
    }
    break;
    */
    }
    STACK_CHECK(L2, 1);
}

// #################################################################################################

#if USE_DEBUG_SPEW()
static char const* lua_type_names[] = {
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
static char const* vt_names[] = {
      "VT::NORMAL"
    , "VT::KEY"
    , "VT::METATABLE"
};
#endif // USE_DEBUG_SPEW()

// #################################################################################################

// Lua 5.4.3 style of dumping (see lstrlib.c)
// we have to do it that way because we can't unbalance the stack between buffer operations
// namely, this means we can't push a function on top of the stack *after* we initialize the buffer!
// luckily, this also works with earlier Lua versions
[[nodiscard]] static int buf_writer(lua_State* L_, void const* b_, size_t size_, void* ud_)
{
    luaL_Buffer* const B{ static_cast<luaL_Buffer*>(ud_) };
    if (!B->L) {
        luaL_buffinit(L_, B);
    }
    luaL_addlstring(B, static_cast<char const*>(b_), size_);
    return 0;
}

// #################################################################################################

// Copy a function over, which has not been found in the cache.
// L2 has the cache key for this function at the top of the stack
void InterCopyContext::copy_func() const
{
    LUA_ASSERT(L1, L2_cache_i != 0);                                                               //                                                L2: ... {cache} ... p
    STACK_GROW(L1, 2);
    STACK_CHECK_START_REL(L1, 0);

    // 'lua_dump()' needs the function at top of stack
    // if already on top of the stack, no need to push again
    bool const needToPush{ L1_i != lua_gettop(L1) };
    if (needToPush) {
        lua_pushvalue(L1, L1_i);                                                                   // L1: ... f
    }

    //
    // "value returned is the error code returned by the last call
    // to the writer" (and we only return 0)
    // not sure this could ever fail but for memory shortage reasons
    // last parameter is Lua 5.4-specific (no stripping)
    luaL_Buffer B;
    B.L = nullptr;
    if (lua504_dump(L1, buf_writer, &B, 0) != 0) {
        raise_luaL_error(getErrL(), "internal error: function dump failed.");
    }

    // pushes dumped string on 'L1'
    luaL_pushresult(&B);                                                                           // L1: ... f b

    // if not pushed, no need to pop
    if (needToPush) {
        lua_remove(L1, -2);                                                                        // L1: ... b
    }

    // transfer the bytecode, then the upvalues, to create a similar closure
    {
        char const* fname = nullptr;
#define LOG_FUNC_INFO 0
#if LOG_FUNC_INFO
        // "To get information about a function you push it onto the
        // stack and start the what string with the character '>'."
        //
        {
            lua_Debug ar;
            lua_pushvalue(L1, L1_i);                                                               // L1: ... b f
            // fills 'fname' 'namewhat' and 'linedefined', pops function
            lua_getinfo(L1, ">nS", &ar);                                                           // L1: ... b
            fname = ar.namewhat;
            DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "FNAME: %s @ %d" INDENT_END(U), ar.short_src, ar.linedefined)); // just gives nullptr
        }
#endif // LOG_FUNC_INFO
        {
            size_t sz;
            char const* s = lua_tolstring(L1, -1, &sz);                                            // L1: ... b
            LUA_ASSERT(L1, s && sz);
            STACK_GROW(L2, 2);
            // Note: Line numbers seem to be taken precisely from the
            //       original function. 'fname' is not used since the chunk
            //       is precompiled (it seems...).
            //
            // TBD: Can we get the function's original name through, as well?
            //
            if (luaL_loadbuffer(L2, s, sz, fname) != 0) {                                           //                                                L2: ... {cache} ... p function
                // chunk is precompiled so only LUA_ERRMEM can happen
                // "Otherwise, it pushes an error message"
                //
                STACK_GROW(L1, 1);
                raise_luaL_error(getErrL(), "%s: %s", fname, lua_tostring(L2, -1));
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
         */
        int n{ 0 };
        {
            InterCopyContext c{ U, L2, L1, L2_cache_i, {}, VT::NORMAL, mode, {} };
#if LUA_VERSION_NUM >= 502
            // Starting with Lua 5.2, each Lua function gets its environment as one of its upvalues (named LUA_ENV, aka "_ENV" by default)
            // Generally this is LUA_RIDX_GLOBALS, which we don't want to copy from the source to the destination state...
            // -> if we encounter an upvalue equal to the global table in the source, bind it to the destination's global table
            lua_pushglobaltable(L1);                                                               // L1: ... _G
#endif // LUA_VERSION_NUM
            for (n = 0; (c.name = lua_getupvalue(L1, L1_i, 1 + n)) != nullptr; ++n) {              // L1: ... _G up[n]
                DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "UPNAME[%d]: %s -> " INDENT_END(U), n, c.name));
#if LUA_VERSION_NUM >= 502
                if (lua_rawequal(L1, -1, -2)) { // is the upvalue equal to the global table?
                    DEBUGSPEW_CODE(fprintf(stderr, "pushing destination global scope\n"));
                    lua_pushglobaltable(L2);                                                       //                                                L2: ... {cache} ... function <upvalues>
                } else
#endif // LUA_VERSION_NUM
                {
                    DEBUGSPEW_CODE(fprintf(stderr, "copying value\n"));
                    c.L1_i = SourceIndex{ lua_gettop(L1) };
                    if (!c.inter_copy_one()) {                                                     //                                                L2: ... {cache} ... function <upvalues>
                        raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
                    }
                }
                lua_pop(L1, 1);                                                                    // L1: ... _G
            }
#if LUA_VERSION_NUM >= 502
            lua_pop(L1, 1);                                                                        // L1: ...
#endif // LUA_VERSION_NUM
        }
                                                                                                   //                                                L2: ... {cache} ... function + 'n' upvalues (>=0)

        STACK_CHECK(L1, 0);

        // Set upvalues (originally set to 'nil' by 'lua_load')
        for (int const func_index{ lua_gettop(L2) - n }; n > 0; --n) {
            char const* rc{ lua_setupvalue(L2, func_index, n) };                                   //                                                L2: ... {cache} ... function
            //
            // "assigns the value at the top of the stack to the upvalue and returns its name.
            // It also pops the value from the stack."

            LUA_ASSERT(L1, rc); // not having enough slots?
        }
        // once all upvalues have been set we are left
        // with the function at the top of the stack                                               //                                                L2: ... {cache} ... function
    }
    STACK_CHECK(L1, 0);
}

// #################################################################################################

// Check if we've already copied the same function from 'L1', and reuse the old copy.
// Always pushes a function to 'L2'.
void InterCopyContext::copy_cached_func() const
{
    FuncSubType const funcSubType{ luaG_getfuncsubtype(L1, L1_i) };
    if (funcSubType == FuncSubType::Bytecode) {
        void* const aspointer = const_cast<void*>(lua_topointer(L1, L1_i));
        // TBD: Merge this and same code for tables
        LUA_ASSERT(L1, L2_cache_i != 0);

        STACK_GROW(L2, 2);

        // L2_cache[id_str]= function
        //
        STACK_CHECK_START_REL(L2, 0);

        // We don't need to use the from state ('L1') in ID since the life span
        // is only for the duration of a copy (both states are locked).

        // push a light userdata uniquely representing the function
        lua_pushlightuserdata(L2, aspointer);                                                      //                                                L2: ... {cache} ... p

        // fprintf( stderr, "<< ID: %s >>\n", lua_tostring( L2, -1));

        lua_pushvalue(L2, -1);                                                                     //                                                L2: ... {cache} ... p p
        lua_rawget(L2, L2_cache_i);                                                                //                                                L2: ... {cache} ... p function|nil|true

        if (lua_isnil(L2, -1)) { // function is unknown
            lua_pop(L2, 1);                                                                        //                                                L2: ... {cache} ... p

            // Set to 'true' for the duration of creation; need to find self-references
            // via upvalues
            //
            // pushes a copy of the func, stores a reference in the cache
            copy_func();                                                                           //                                                L2: ... {cache} ... function
        } else { // found function in the cache
            lua_remove(L2, -2); // L2: ... {cache} ... function
        }
        STACK_CHECK(L2, 1);
        LUA_ASSERT(L1, lua_isfunction(L2, -1));
    } else { // function is native/LuaJIT: no need to cache
        lookup_native_func();                                                                      //                                                L2: ... {cache} ... function
        // if the function was in fact a lookup sentinel, we can either get a function or a table here
        LUA_ASSERT(L1, lua_isfunction(L2, -1) || lua_istable(L2, -1));
    }
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::push_cached_metatable() const
{
    STACK_CHECK_START_REL(L1, 0);
    if (!lua_getmetatable(L1, L1_i)) {                                                             // L1: ... mt
        STACK_CHECK(L1, 0);
        return false;
    }
    STACK_CHECK(L1, 1);

    lua_Integer const mt_id{ get_mt_id(U, L1, -1) }; // Unique id for the metatable

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 4);
    // do we already know this metatable?
    std::ignore = kMtIdRegKey.getSubTable(L2, 0, 0);                                               //                                                L2: _R[kMtIdRegKey]
    lua_pushinteger(L2, mt_id);                                                                    //                                                L2: _R[kMtIdRegKey] id
    lua_rawget(L2, -2);                                                                            //                                                L2: _R[kMtIdRegKey] mt|nil
    STACK_CHECK(L2, 2);

    if (lua_isnil(L2, -1)) { // L2 did not know the metatable
        lua_pop(L2, 1);                                                                            //                                                L2: _R[kMtIdRegKey]
        InterCopyContext const c{ U, L2, L1, L2_cache_i, SourceIndex{ lua_gettop(L1) }, VT::METATABLE, mode, name };
        if (!c.inter_copy_one()) {                                                                 //                                                L2: _R[kMtIdRegKey] mt?
            raise_luaL_error(getErrL(), "Error copying a metatable");
        }

        STACK_CHECK(L2, 2);                                                                        //                                                L2: _R[kMtIdRegKey] mt
        // mt_id -> metatable
        lua_pushinteger(L2, mt_id);                                                                //                                                L2: _R[kMtIdRegKey] mt id
        lua_pushvalue(L2, -2);                                                                     //                                                L2: _R[kMtIdRegKey] mt id mt
        lua_rawset(L2, -4);                                                                        //                                                L2: _R[kMtIdRegKey] mt

        // metatable -> mt_id
        lua_pushvalue(L2, -1);                                                                     //                                                L2: _R[kMtIdRegKey] mt mt
        lua_pushinteger(L2, mt_id);                                                                //                                                L2: _R[kMtIdRegKey] mt mt id
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

void InterCopyContext::inter_copy_keyvaluepair() const
{
    SourceIndex const val_i{ lua_gettop(L1) };
    SourceIndex const key_i{ val_i - 1 };

    // For the key, only basic key types are copied over. others ignored
    InterCopyContext c{ U, L2, L1, L2_cache_i, key_i, VT::KEY, mode, name };
    if (!c.inter_copy_one()) {
        return;
        // we could raise an error instead of ignoring the table entry, like so:
        // raise_luaL_error(L1, "Unable to copy %s key '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", name, luaL_typename(L1, key_i));
        // maybe offer this possibility as a global configuration option, or a linda setting, or as a parameter of the call causing the transfer?
    }

    char* valPath{ nullptr };
    if (U->verboseErrors) {
        // for debug purposes, let's try to build a useful name
        if (lua_type(L1, key_i) == LUA_TSTRING) {
            char const* key{ lua_tostring(L1, key_i) };
            size_t const keyRawLen = lua_rawlen(L1, key_i);
            size_t const bufLen = strlen(name) + keyRawLen + 2;
            valPath = (char*) alloca(bufLen);
            sprintf(valPath, "%s.%*s", name, (int) keyRawLen, key);
            key = nullptr;
        }
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (lua_isinteger(L1, key_i)) {
            lua_Integer const key{ lua_tointeger(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 32 + 3);
            sprintf(valPath, "%s[" LUA_INTEGER_FMT "]", name, key);
        }
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (lua_type(L1, key_i) == LUA_TNUMBER) {
            lua_Number const key{ lua_tonumber(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 32 + 3);
            sprintf(valPath, "%s[" LUA_NUMBER_FMT "]", name, key);
        } else if (lua_type(L1, key_i) == LUA_TLIGHTUSERDATA) {
            void* const key{ lua_touserdata(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 16 + 5);
            sprintf(valPath, "%s[U:%p]", name, key);
        } else if (lua_type(L1, key_i) == LUA_TBOOLEAN) {
            int const key{ lua_toboolean(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 8);
            sprintf(valPath, "%s[%s]", name, key ? "true" : "false");
        }
    }
    c.L1_i = SourceIndex{ val_i };
    // Contents of metatables are copied with cache checking. important to detect loops.
    c.vt = VT::NORMAL;
    c.name = valPath ? valPath : name;
    if (c.inter_copy_one()) {
        LUA_ASSERT(L1, lua_istable(L2, -3));
        lua_rawset(L2, -3); // add to table (pops key & val)
    } else {
        raise_luaL_error(getErrL(), "Unable to copy %s entry '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", valPath, luaL_typename(L1, val_i));
    }
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::tryCopyClonable() const
{
    SourceIndex const L1i{ lua_absindex(L1, L1_i) };
    void* const source{ lua_touserdata(L1, L1i) };

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    // Check if the source was already cloned during this copy
    lua_pushlightuserdata(L2, source);                                                             //                                                L2: ... source
    lua_rawget(L2, L2_cache_i);                                                                    //                                                L2: ... clone?
    if (!lua_isnil(L2, -1)) {
        STACK_CHECK(L2, 1);
        return true;
    } else {
        lua_pop(L2, 1);                                                                            //                                                L2: ...
    }
    STACK_CHECK(L2, 0);

    // no metatable? -> not clonable
    if (!lua_getmetatable(L1, L1i)) {                                                              // L1: ... mt?
        STACK_CHECK(L1, 0);
        return false;
    }

    // no __lanesclone? -> not clonable
    lua_getfield(L1, -1, "__lanesclone");                                                          // L1: ... mt __lanesclone?
    if (lua_isnil(L1, -1)) {
        lua_pop(L1, 2);                                                                            // L1: ...
        STACK_CHECK(L1, 0);
        return false;
    }

    // we need to copy over the uservalues of the userdata as well
    {
        int const mt{ lua_absindex(L1, -2) };                                                      // L1: ... mt __lanesclone
        size_t const userdata_size{ lua_rawlen(L1, L1i) };
        // extract all the uservalues, but don't transfer them yet
        int uvi = 0;
        while (lua_getiuservalue(L1, L1i, ++uvi) != LUA_TNONE) {}                                  // L1: ... mt __lanesclone [uv]+ nil
        // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
        lua_pop(L1, 1);                                                                            // L1: ... mt __lanesclone [uv]+
        --uvi;
        // create the clone userdata with the required number of uservalue slots
        void* const clone{ lua_newuserdatauv(L2, userdata_size, uvi) };                            //                                                L2: ... u
        // copy the metatable in the target state, and give it to the clone we put there
        InterCopyContext c{ U, L2, L1, L2_cache_i, SourceIndex{ mt }, VT::NORMAL, mode, name };
        if (c.inter_copy_one()) {                                                                  //                                                L2: ... u mt|sentinel
            if (LookupMode::ToKeeper == mode) {                                                    //                                                L2: ... u sentinel
                LUA_ASSERT(L1, lua_tocfunction(L2, -1) == table_lookup_sentinel);
                // we want to create a new closure with a 'clone sentinel' function, where the upvalues are the userdata and the metatable fqn
                lua_getupvalue(L2, -1, 1);                                                         //                                                L2: ... u sentinel fqn
                lua_remove(L2, -2);                                                                //                                                L2: ... u fqn
                lua_insert(L2, -2);                                                                //                                                L2: ... fqn u
                lua_pushcclosure(L2, userdata_clone_sentinel, 2);                                  //                                                L2: ... userdata_clone_sentinel
            } else { // from keeper or direct                                                      //                                                L2: ... u mt
                LUA_ASSERT(L1, lua_istable(L2, -1));
                lua_setmetatable(L2, -2);                                                          //                                                L2: ... u
            }
            STACK_CHECK(L2, 1);
        } else {
            raise_luaL_error(getErrL(), "Error copying a metatable");
        }
        // first, add the entry in the cache (at this point it is either the actual userdata or the keeper sentinel
        lua_pushlightuserdata(L2, source);                                                         //                                                L2: ... u source
        lua_pushvalue(L2, -2);                                                                     //                                                L2: ... u source u
        lua_rawset(L2, L2_cache_i);                                                                //                                                L2: ... u
        // make sure we have the userdata now
        if (LookupMode::ToKeeper == mode) {                                                        //                                                L2: ... userdata_clone_sentinel
            lua_getupvalue(L2, -1, 2);                                                             //                                                L2: ... userdata_clone_sentinel u
        }
        // assign uservalues
        while (uvi > 0) {
            c.L1_i = SourceIndex{ lua_absindex(L1, -1) };
            if (!c.inter_copy_one()) {                                                             //                                                L2: ... u uv
                raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
            }
            lua_pop(L1, 1);                                                                        // L1: ... mt __lanesclone [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, -2, uvi);                                                        //                                                L2: ... u
            --uvi;
        }
        // when we are done, all uservalues are popped from the source stack, and we want only the single transferred value in the destination
        if (LookupMode::ToKeeper == mode) {                                                        //                                                L2: ... userdata_clone_sentinel u
            lua_pop(L2, 1);                                                                        //                                                L2: ... userdata_clone_sentinel
        }
        STACK_CHECK(L2, 1);
        STACK_CHECK(L1, 2);
        // call cloning function in source state to perform the actual memory cloning
        lua_pushlightuserdata(L1, clone);                                                          // L1: ... mt __lanesclone clone
        lua_pushlightuserdata(L1, source);                                                         // L1: ... mt __lanesclone clone source
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

[[nodiscard]] bool InterCopyContext::inter_copy_userdata() const
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
    DEBUGSPEW_CODE(fprintf(stderr, "USERDATA\n"));
    if (tryCopyDeep()) {
        STACK_CHECK(L1, 0);
        STACK_CHECK(L2, 1);
        return true;
    }

    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 0);

    // Not a deep or clonable full userdata
    if (U->demoteFullUserdata) { // attempt demotion to light userdata
        void* const lud{ lua_touserdata(L1, L1_i) };
        lua_pushlightuserdata(L2, lud);
    } else { // raise an error
        raise_luaL_error(getErrL(), "can't copy non-deep full userdata across lanes");
    }

    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_function() const
{
    if (vt == VT::KEY) {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(fprintf(stderr, "FUNCTION %s\n", name));

    if (lua_tocfunction(L1, L1_i) == userdata_clone_sentinel) { // we are actually copying a clonable full userdata from a keeper
        // clone the full userdata again

        // let's see if we already restored this userdata
        lua_getupvalue(L1, L1_i, 2);                                                               // L1: ... u
        void* source = lua_touserdata(L1, -1);
        lua_pushlightuserdata(L2, source);                                                         //                                                L2: ... source
        lua_rawget(L2, L2_cache_i);                                                                //                                                L2: ... u?
        if (!lua_isnil(L2, -1)) {
            lua_pop(L1, 1);                                                                        // L1: ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 1);
            return true;
        }
        lua_pop(L2, 1);                                                                            //                                                L2: ...

        // userdata_clone_sentinel has 2 upvalues: the fqn of its metatable, and the userdata itself
        bool const found{ lookup_table() };                                                        //                                                L2: ... mt?
        if (!found) {
            STACK_CHECK(L2, 0);
            return false;
        }
        // 'L1_i' slot was the proxy closure, but from now on we operate onthe actual userdata we extracted from it
        SourceIndex const source_i{ lua_gettop(L1) };
        source = lua_touserdata(L1, -1);
        void* clone{ nullptr };
        // get the number of bytes to allocate for the clone
        size_t const userdata_size{ lua_rawlen(L1, -1) };
        {
            // extract uservalues (don't transfer them yet)
            int uvi = 0;
            while (lua_getiuservalue(L1, source_i, ++uvi) != LUA_TNONE) {}                         // L1: ... u uv
            // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
            lua_pop(L1, 1);                                                                        // L1: ... u [uv]*
            --uvi;
            STACK_CHECK(L1, uvi + 1);
            // create the clone userdata with the required number of uservalue slots
            clone = lua_newuserdatauv(L2, userdata_size, uvi);                                     //                                                L2: ... mt u
            // add it in the cache
            lua_pushlightuserdata(L2, source);                                                     //                                                L2: ... mt u source
            lua_pushvalue(L2, -2);                                                                 //                                                L2: ... mt u source u
            lua_rawset(L2, L2_cache_i);                                                            //                                                L2: ... mt u
            // set metatable
            lua_pushvalue(L2, -2);                                                                 //                                                L2: ... mt u mt
            lua_setmetatable(L2, -2);                                                              //                                                L2: ... mt u
            // transfer and assign uservalues
            InterCopyContext c{ *this };
            while (uvi > 0) {
                c.L1_i = SourceIndex{ lua_absindex(L1, -1) };
                if (!c.inter_copy_one()) {                                                         //                                                L2: ... mt u uv
                    raise_luaL_error(getErrL(), "Cannot copy upvalue type '%s'", luaL_typename(L1, -1));
                }
                lua_pop(L1, 1);                                                                    // L1: ... u [uv]*
                // this pops the value from the stack
                lua_setiuservalue(L2, -2, uvi);                                                    //                                                L2: ... mt u
                --uvi;
            }
            // when we are done, all uservalues are popped from the stack, we can pop the source as well
            lua_pop(L1, 1);                                                                        // L1: ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 2);                                                                    //                                                L2: ... mt u
        }
        // perform the custom cloning part
        lua_insert(L2, -2); // L2: ... u mt
        // __lanesclone should always exist because we wouldn't be restoring data from a userdata_clone_sentinel closure to begin with
        lua_getfield(L2, -1, "__lanesclone");                                                      //                                                L2: ... u mt __lanesclone
        lua_remove(L2, -2);                                                                        //                                                L2: ... u __lanesclone
        lua_pushlightuserdata(L2, clone);                                                          //                                                L2: ... u __lanesclone clone
        lua_pushlightuserdata(L2, source);                                                         //                                                L2: ... u __lanesclone clone source
        lua_pushinteger(L2, userdata_size);                                                        //                                                L2: ... u __lanesclone clone source size
        // clone:__lanesclone(dest, source, size)
        lua_call(L2, 3, 0);                                                                        //                                                L2: ... u
    } else { // regular function
        DEBUGSPEW_CODE(fprintf(stderr, "FUNCTION %s\n", name));
        DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
        copy_cached_func(); // L2: ... f
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_table() const
{
    if (vt == VT::KEY) {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(fprintf(stderr, "TABLE %s\n", name));

    /*
     * First, let's try to see if this table is special (aka is it some table that we registered in our lookup databases during module registration?)
     * Note that this table CAN be a module table, but we just didn't register it, in which case we'll send it through the table cloning mechanism
     */
    if (lookup_table()) {
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
    if (push_cached_table()) {
        LUA_ASSERT(L1, lua_istable(L2, -1)); // from cache
        return true;
    }
    LUA_ASSERT(L1, lua_istable(L2, -1));

    STACK_GROW(L1, 2);
    STACK_GROW(L2, 2);

    lua_pushnil(L1); // start iteration
    while (lua_next(L1, L1_i)) {
        // need a function to prevent overflowing the stack with verboseErrors-induced alloca()
        inter_copy_keyvaluepair();
        lua_pop(L1, 1); // pop value (next round)
    }
    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 1);

    // Metatables are expected to be immutable, and copied only once.
    if (push_cached_metatable()) {                                                                 //                                                L2: ... t mt?
        lua_setmetatable(L2, -2);                                                                  //                                                L2: ... t
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_boolean() const
{
    int const v{ lua_toboolean(L1, L1_i) };
    DEBUGSPEW_CODE(fprintf(stderr, "%s\n", v ? "true" : "false"));
    lua_pushboolean(L2, v);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_lightuserdata() const
{
    void* const p{ lua_touserdata(L1, L1_i) };
    DEBUGSPEW_CODE(fprintf(stderr, "%p\n", p));
    lua_pushlightuserdata(L2, p);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_nil() const
{
    if (vt == VT::KEY) {
        return false;
    }
    lua_pushnil(L2);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_number() const
{
    // LNUM patch support (keeping integer accuracy)
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
    if (lua_isinteger(L1, L1_i)) {
        lua_Integer const v{ lua_tointeger(L1, L1_i) };
        DEBUGSPEW_CODE(fprintf(stderr, LUA_INTEGER_FMT "\n", v));
        lua_pushinteger(L2, v);
    } else
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
    {
        lua_Number const v{ lua_tonumber(L1, L1_i) };
        DEBUGSPEW_CODE(fprintf(stderr, LUA_NUMBER_FMT "\n", v));
        lua_pushnumber(L2, v);
    }
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_string() const
{
    size_t len;
    char const* const s{ lua_tolstring(L1, L1_i, &len) };
    DEBUGSPEW_CODE(fprintf(stderr, "'%s'\n", s));
    lua_pushlstring(L2, s, len);
    return true;
}

// #################################################################################################

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
[[nodiscard]] bool InterCopyContext::inter_copy_one() const
{
    static constexpr int kPODmask = (1 << LUA_TNIL) | (1 << LUA_TBOOLEAN) | (1 << LUA_TLIGHTUSERDATA) | (1 << LUA_TNUMBER) | (1 << LUA_TSTRING);
    STACK_GROW(L2, 1);
    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);

    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "inter_copy_one()\n" INDENT_END(U)));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    LuaType val_type{ lua_type_as_enum(L1, L1_i) };
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%s %s: " INDENT_END(U), lua_type_names[static_cast<int>(val_type)], vt_names[static_cast<int>(vt)]));

    // Non-POD can be skipped if its metatable contains { __lanesignore = true }
    if (((1 << static_cast<int>(val_type)) & kPODmask) == 0) {
        if (lua_getmetatable(L1, L1_i)) {                                                          // L1: ... mt
            lua_getfield(L1, -1, "__lanesignore"); // L1: ... mt ignore?
            if (lua_isboolean(L1, -1) && lua_toboolean(L1, -1)) {
                DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "__lanesignore -> LUA_TNIL\n" INDENT_END(U)));
                val_type = LuaType::NIL;
            }
            lua_pop(L1, 2);                                                                        // L1: ...
        }
    }
    STACK_CHECK(L1, 0);

    // Lets push nil to L2 if the object should be ignored
    bool ret{ true };
    switch (val_type) {
    // Basic types allowed both as values, and as table keys
    case LuaType::BOOLEAN:
        ret = inter_copy_boolean();
        break;
    case LuaType::NUMBER:
        ret = inter_copy_number();
        break;
    case LuaType::STRING:
        ret = inter_copy_string();
        break;
    case LuaType::LIGHTUSERDATA:
        ret = inter_copy_lightuserdata();
        break;

    // The following types are not allowed as table keys
    case LuaType::USERDATA:
        ret = inter_copy_userdata();
        break;
    case LuaType::NIL:
        ret = inter_copy_nil();
        break;
    case LuaType::FUNCTION:
        ret = inter_copy_function();
        break;
    case LuaType::TABLE:
        ret = inter_copy_table();
        break;

    // The following types cannot be copied
    case LuaType::CDATA:
        [[fallthrough]];
    case LuaType::THREAD:
        ret = false;
        break;
    }

    STACK_CHECK(L2, ret ? 1 : 0);
    STACK_CHECK(L1, 0);
    return ret;
}

// #################################################################################################

// Akin to 'lua_xmove' but copies values between _any_ Lua states.
// NOTE: Both the states must be solely in the current OS thread's possession.
[[nodiscard]] InterCopyResult InterCopyContext::inter_copy(int n_) const
{
    LUA_ASSERT(L1, vt == VT::NORMAL);

    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "InterCopyContext::inter_copy()\n" INDENT_END(U)));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    int const top_L1{ lua_gettop(L1) };
    if (n_ > top_L1) {
        // requesting to copy more than is available?
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "nothing to copy()\n" INDENT_END(U)));
        return InterCopyResult::NotEnoughValues;
    }

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, n_ + 1);

    /*
     * Make a cache table for the duration of this copy. Collects tables and
     * function entries, avoiding the same entries to be passed on as multiple
     * copies. ESSENTIAL i.e. for handling upvalue tables in the right manner!
     */
    int const top_L2{ lua_gettop(L2) };                                                            //                                                L2: ...
    lua_newtable(L2);                                                                              //                                                L2: ... cache

    char tmpBuf[16];
    char const* const pBuf{ U->verboseErrors ? tmpBuf : "?" };
    InterCopyContext c{ U, L2, L1, CacheIndex{ top_L2 + 1 }, {}, VT::NORMAL, mode, pBuf };
    bool copyok{ true };
    STACK_CHECK_START_REL(L1, 0);
    for (int i{ top_L1 - n_ + 1 }, j{ 1 }; i <= top_L1; ++i, ++j) {
        if (U->verboseErrors) {
            sprintf(tmpBuf, "arg_%d", j);
        }
        c.L1_i = SourceIndex{ i };
        copyok = c.inter_copy_one();                                                               //                                                L2: ... cache {}n
        if (!copyok) {
            break;
        }
    }
    STACK_CHECK(L1, 0);

    if (copyok) {
        STACK_CHECK(L2, n_ + 1);
        // Remove the cache table. Persistent caching would cause i.e. multiple
        // messages passed in the same table to use the same table also in receiving end.
        lua_remove(L2, top_L2 + 1);
        return InterCopyResult::Success;
    }

    // error -> pop everything from the target state stack
    lua_settop(L2, top_L2);
    STACK_CHECK(L2, 0);
    return InterCopyResult::Error;
}

// #################################################################################################

[[nodiscard]] InterCopyResult InterCopyContext::inter_move(int n_) const
{
    InterCopyResult const ret{ inter_copy(n_) };
    lua_pop(L1, n_);
    return ret;
}

// #################################################################################################

// transfers stuff from L1->_G["package"] to L2->_G["package"]
// returns InterCopyResult::Success if everything is fine
// returns InterCopyResult::Error if pushed an error message in L1
// else raise an error in L1
[[nodiscard]] InterCopyResult InterCopyContext::inter_copy_package() const
{
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "InterCopyContext::inter_copy_package()\n" INDENT_END(U)));

    class OnExit
    {
        private:
        lua_State* const L2;
        int const top_L2;
        DEBUGSPEW_CODE(DebugSpewIndentScope m_scope);

        public:
        OnExit(DEBUGSPEW_PARAM_COMMA(Universe* U_) lua_State* L2_)
        : L2{ L2_ }
        , top_L2{ lua_gettop(L2) } DEBUGSPEW_COMMA_PARAM(m_scope{ U_ })
        {
        }

        ~OnExit()
        {
            lua_settop(L2, top_L2);
        }
    } onExit{ DEBUGSPEW_PARAM_COMMA(U) L2 };

    STACK_CHECK_START_REL(L1, 0);
    if (lua_type_as_enum(L1, L1_i) != LuaType::TABLE) {
        lua_pushfstring(L1, "expected package as table, got %s", luaL_typename(L1, L1_i));
        STACK_CHECK(L1, 1);
        // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
        if (mode == LookupMode::LaneBody) {
            raise_lua_error(getErrL()); // that's ok, getErrL() is L1 in that case
        }
        return InterCopyResult::Error;
    }
    if (luaG_getmodule(L2, LUA_LOADLIBNAME) == LuaType::NIL) { // package library not loaded: do nothing
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "'package' not loaded, nothing to do\n" INDENT_END(U)));
        STACK_CHECK(L1, 0);
        return InterCopyResult::Success;
    }

    InterCopyResult result{ InterCopyResult::Success };
    // package.loaders is renamed package.searchers in Lua 5.2
    // but don't copy it anyway, as the function names change depending on the slot index!
    // users should provide an on_state_create function to setup custom loaders instead
    // don't copy package.preload in keeper states (they don't know how to translate functions)
    char const* entries[] = { "path", "cpath", (mode == LookupMode::LaneBody) ? "preload" : nullptr /*, (LUA_VERSION_NUM == 501) ? "loaders" : "searchers"*/, nullptr };
    for (char const* const entry : entries) {
        if (!entry) {
            continue;
        }
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "package.%s\n" INDENT_END(U), entry));
        lua_getfield(L1, L1_i, entry);
        if (lua_isnil(L1, -1)) {
            lua_pop(L1, 1);
        } else {
            {
                DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
                result = inter_move(1); // moves the entry to L2
                STACK_CHECK(L1, 0);
            }
            if (result == InterCopyResult::Success) {
                lua_setfield(L2, -2, entry); // set package[entry]
            } else {
                lua_pushfstring(L1, "failed to copy package entry %s", entry);
                // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
                if (mode == LookupMode::LaneBody) {
                    raise_lua_error(getErrL());
                }
                lua_pop(L1, 1);
                break;
            }
        }
    }
    STACK_CHECK(L1, 0);
    return result;
}
