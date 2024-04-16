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

// functions implemented in deep.c
extern void push_registry_subtable( lua_State* L, UniqueKey key_);

DEBUGSPEW_CODE(char const* const DebugSpewIndentScope::debugspew_indent = "----+----!----+----!----+----!----+----!----+----!----+----!----+----!----+");


// ################################################################################################

/*
 * Does what the original 'push_registry_subtable' function did, but adds an optional mode argument to it
 */
void push_registry_subtable_mode( lua_State* L, UniqueKey key_, const char* mode_)
{
    STACK_GROW(L, 3);
    STACK_CHECK_START_REL(L, 0);

    key_.pushValue(L);                                                    // {}|nil
    STACK_CHECK(L, 1);

    if (lua_isnil(L, -1))
    {
        lua_pop(L, 1);                                                    //
        lua_newtable(L);                                                  // {}
        // _R[key_] = {}
        key_.setValue(L, [](lua_State* L) { lua_pushvalue(L, -2); });     // {}
        STACK_CHECK(L, 1);

        // Set its metatable if requested
        if (mode_)
        {
            lua_newtable(L);                                              // {} mt
            lua_pushliteral(L, "__mode");                                 // {} mt "__mode"
            lua_pushstring(L, mode_);                                     // {} mt "__mode" mode
            lua_rawset(L, -3);                                            // {} mt
            lua_setmetatable(L, -2);                                      // {}
        }
    }
    STACK_CHECK(L, 1);
    ASSERT_L(lua_istable(L, -1));
}

// ################################################################################################

/*
 * Push a registry subtable (keyed by unique 'key_') onto the stack.
 * If the subtable does not exist, it is created and chained.
 */
void push_registry_subtable( lua_State* L, UniqueKey key_)
{
  push_registry_subtable_mode(L, key_, nullptr);
}

// ################################################################################################

/*---=== luaG_dump ===---*/
#ifdef _DEBUG
void luaG_dump( lua_State* L)
{
    int top = lua_gettop( L);
    int i;

    fprintf( stderr, "\n\tDEBUG STACK:\n");

    if( top == 0)
        fprintf( stderr, "\t(none)\n");

    for( i = 1; i <= top; ++ i)
    {
        LuaType type{ lua_type_as_enum(L, i) };

        fprintf( stderr, "\t[%d]= (%s) ", i, lua_typename( L, type));

        // Print item contents here...
        //
        // Note: this requires 'tostring()' to be defined. If it is NOT,
        //       enable it for more debugging.
        //
        STACK_CHECK_START_REL(L, 0);
        STACK_GROW( L, 2);

        lua_getglobal( L, "tostring");
        //
        // [-1]: tostring function, or nil

        if( !lua_isfunction( L, -1))
        {
            fprintf( stderr, "('tostring' not available)");
        }
        else
        {
            lua_pushvalue( L, i);
            lua_call( L, 1 /*args*/, 1 /*retvals*/);

            // Don't trust the string contents
            //                
            fprintf( stderr, "%s", lua_tostring( L, -1));
        }
        lua_pop( L, 1);
        STACK_CHECK( L, 0);
        fprintf( stderr, "\n");
    }
    fprintf( stderr, "\n");
}
#endif // _DEBUG

// ################################################################################################

// same as PUC-Lua l_alloc
extern "C" [[nodiscard]] static void* libc_lua_Alloc([[maybe_unused]] void* ud, [[maybe_unused]] void* ptr_, [[maybe_unused]] size_t osize_, size_t nsize_)
{
    if (nsize_ == 0)
    {
        free(ptr_);
        return nullptr;
    }
    else
    {
        return realloc(ptr_, nsize_);
    }
}

// #################################################################################################

[[nodiscard]] static int luaG_provide_protected_allocator(lua_State* L)
{
    Universe* const U{ universe_get(L) };
    // push a new full userdata on the stack, giving access to the universe's protected allocator
    [[maybe_unused]] AllocatorDefinition* const def{ new (L) AllocatorDefinition{ U->protected_allocator.makeDefinition() } };
    return 1;
}

// #################################################################################################

// called once at the creation of the universe (therefore L is the master Lua state everything originates from)
// Do I need to disable this when compiling for LuaJIT to prevent issues?
void initialize_allocator_function(Universe* U, lua_State* L)
{
    STACK_CHECK_START_REL(L, 1);                            // settings
    lua_getfield(L, -1, "allocator");                       // settings allocator|nil|"protected"
    if (!lua_isnil(L, -1))
    {
        // store C function pointer in an internal variable
        U->provide_allocator = lua_tocfunction(L, -1);      // settings allocator
        if (U->provide_allocator != nullptr)
        {
            // make sure the function doesn't have upvalues
            char const* upname = lua_getupvalue(L, -1, 1);  // settings allocator upval?
            if (upname != nullptr) // should be "" for C functions with upvalues if any
            {
                (void) luaL_error(L, "config.allocator() shouldn't have upvalues");
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L);                                 // settings allocator nil
            lua_setfield(L, -3, "allocator");               // settings allocator
        }
        else if (lua_type(L, -1) == LUA_TSTRING) // should be "protected"
        {
            ASSERT_L(strcmp(lua_tostring(L, -1), "protected") == 0);
            // set the original allocator to call from inside protection by the mutex
            U->protected_allocator.initFrom(L);
            U->protected_allocator.installIn(L);
            // before a state is created, this function will be called to obtain the allocator
            U->provide_allocator = luaG_provide_protected_allocator;
        }
    }
    else
    {
        // just grab whatever allocator was provided to lua_newstate
        U->protected_allocator.initFrom(L);
    }
    lua_pop(L, 1);                                          // settings
    STACK_CHECK(L, 1);

    lua_getfield(L, -1, "internal_allocator");              // settings "libc"|"allocator"
    {
        char const* allocator = lua_tostring(L, -1);
        if (strcmp(allocator, "libc") == 0)
        {
            U->internal_allocator = AllocatorDefinition{ libc_lua_Alloc, nullptr };
        }
        else if (U->provide_allocator == luaG_provide_protected_allocator)
        {
            // user wants mutex protection on the state's allocator. Use protection for our own allocations too, just in case.
            U->internal_allocator = U->protected_allocator.makeDefinition();
        }
        else
        {
            // no protection required, just use whatever we have as-is.
            U->internal_allocator = U->protected_allocator;
        }
    }
    lua_pop(L, 1);                                          // settings
    STACK_CHECK(L, 1);
}

// ################################################################################################

[[nodiscard]] static int dummy_writer(lua_State* L, void const* p, size_t sz, void* ud)
{
    (void)L; (void)p; (void)sz; (void) ud; // unused
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

enum FuncSubType
{
    FST_Bytecode,
    FST_Native,
    FST_FastJIT
} ;

FuncSubType luaG_getfuncsubtype( lua_State *L, int _i)
{
    if( lua_tocfunction( L, _i))
    {
        return FST_Native;
    }
    {
        int mustpush = 0, dumpres;
        if( lua_absindex( L, _i) != lua_gettop( L))
        {
            lua_pushvalue( L, _i);
            mustpush = 1;
        }
        // the provided writer fails with code 666
        // therefore, anytime we get 666, this means that lua_dump() attempted a dump
        // all other cases mean this is either a C or LuaJIT-fast function
        dumpres = lua504_dump(L, dummy_writer, nullptr, 0);
        lua_pop( L, mustpush);
        if( dumpres == 666)
        {
            return FST_Bytecode;
        }
    }
    return FST_FastJIT;
}

// #################################################################################################

[[nodiscard]] static lua_CFunction luaG_tocfunction(lua_State* L, int _i, FuncSubType* _out)
{
    lua_CFunction p = lua_tocfunction( L, _i);
    *_out = luaG_getfuncsubtype( L, _i);
    return p;
}

// crc64/we of string "LOOKUPCACHE_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey LOOKUPCACHE_REGKEY{ 0x837a68dfc6fcb716ull };

// #################################################################################################

// inspired from tconcat() in ltablib.c
[[nodiscard]] static char const* luaG_pushFQN(lua_State* L, int t, int last, size_t* length)
{
    int i = 1;
    luaL_Buffer b;
    STACK_CHECK_START_REL(L, 0);
    // Lua 5.4 pushes &b as light userdata on the stack. be aware of it...
    luaL_buffinit( L, &b);                            // ... {} ... &b?
    for( ; i < last; ++ i)
    {
        lua_rawgeti( L, t, i);
        luaL_addvalue( &b);
        luaL_addlstring(&b, "/", 1);
    }
    if( i == last)  // add last value (if interval was not empty)
    {
        lua_rawgeti( L, t, i);
        luaL_addvalue( &b);
    }
    // &b is popped at that point (-> replaced by the result)
    luaL_pushresult( &b);                             // ... {} ... "<result>"
    STACK_CHECK( L, 1);
    return lua_tolstring( L, -1, length);
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
static void update_lookup_entry(DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State* L, int _ctx_base, int _depth)
{
    // slot 1 in the stack contains the table that receives everything we found
    int const dest = _ctx_base;
    // slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
    int const fqn = _ctx_base + 1;

    size_t prevNameLength, newNameLength;
    char const* prevName;
    DEBUGSPEW_CODE(char const *newName);
    DEBUGSPEW_CODE(fprintf( stderr, INDENT_BEGIN "update_lookup_entry()\n" INDENT_END));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    STACK_CHECK_START_REL(L, 0);
    // first, raise an error if the function is already known
    lua_pushvalue( L, -1);                                                                // ... {bfc} k o o
    lua_rawget( L, dest);                                                                 // ... {bfc} k o name?
    prevName = lua_tolstring( L, -1, &prevNameLength); // nullptr if we got nil (first encounter of this object)
    // push name in fqn stack (note that concatenation will crash if name is a not string or a number)
    lua_pushvalue( L, -3);                                                                // ... {bfc} k o name? k
    ASSERT_L( lua_type( L, -1) == LUA_TNUMBER || lua_type( L, -1) == LUA_TSTRING);
    ++ _depth;
    lua_rawseti( L, fqn, _depth);                                                         // ... {bfc} k o name?
    // generate name
    DEBUGSPEW_OR_NOT(newName, std::ignore) = luaG_pushFQN(L, fqn, _depth, &newNameLength);// ... {bfc} k o name? "f.q.n"
    // Lua 5.2 introduced a hash randomizer seed which causes table iteration to yield a different key order
    // on different VMs even when the tables are populated the exact same way.
    // When Lua is built with compatibility options (such as LUA_COMPAT_ALL),
    // this causes several base libraries to register functions under multiple names.
    // This, with the randomizer, can cause the first generated name of an object to be different on different VMs,
    // which breaks function transfer.
    // Also, nothing prevents any external module from exposing a given object under several names, so...
    // Therefore, when we encounter an object for which a name was previously registered, we need to select the names
    // based on some sorting order so that we end up with the same name in all databases whatever order the table walk yielded
    if (prevName != nullptr && (prevNameLength < newNameLength || lua_lessthan(L, -2, -1)))
    {
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%s '%s' remained named '%s'\n" INDENT_END, lua_typename( L, lua_type( L, -3)), newName, prevName));
        // the previous name is 'smaller' than the one we just generated: keep it!
        lua_pop( L, 3);                                                                     // ... {bfc} k
    }
    else
    {
        // the name we generated is either the first one, or a better fit for our purposes
        if( prevName)
        {
            // clear the previous name for the database to avoid clutter
            lua_insert( L, -2);                                                               // ... {bfc} k o "f.q.n" prevName
            // t[prevName] = nil
            lua_pushnil( L);                                                                  // ... {bfc} k o "f.q.n" prevName nil
            lua_rawset( L, dest);                                                             // ... {bfc} k o "f.q.n"
        }
        else
        {
            lua_remove( L, -2);                                                               // ... {bfc} k o "f.q.n"
        }
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%s '%s'\n" INDENT_END, lua_typename( L, lua_type( L, -2)), newName));
        // prepare the stack for database feed
        lua_pushvalue( L, -1);                                                              // ... {bfc} k o "f.q.n" "f.q.n"
        lua_pushvalue( L, -3);                                                              // ... {bfc} k o "f.q.n" "f.q.n" o
        ASSERT_L( lua_rawequal( L, -1, -4));
        ASSERT_L( lua_rawequal( L, -2, -3));
        // t["f.q.n"] = o
        lua_rawset( L, dest);                                                               // ... {bfc} k o "f.q.n"
        // t[o] = "f.q.n"
        lua_rawset( L, dest);                                                               // ... {bfc} k
        // remove table name from fqn stack
        lua_pushnil( L);                                                                    // ... {bfc} k nil
        lua_rawseti( L, fqn, _depth);                                                       // ... {bfc} k
    }
    -- _depth;
    STACK_CHECK(L, -1);
}

// #################################################################################################

static void populate_func_lookup_table_recur(DEBUGSPEW_PARAM_COMMA(Universe* U) lua_State* L, int _ctx_base, int _i, int _depth)
{
    lua_Integer visit_count;
    // slot 2 contains a table that, when concatenated, produces the fully qualified name of scanned elements in the table provided at slot _i
    int const fqn = _ctx_base + 1;
    // slot 3 contains a cache that stores all already visited tables to avoid infinite recursion loops
    int const cache = _ctx_base + 2;
    // we need to remember subtables to process them after functions encountered at the current depth (breadth-first search)
    int const breadth_first_cache = lua_gettop( L) + 1;
    DEBUGSPEW_CODE(fprintf( stderr, INDENT_BEGIN "populate_func_lookup_table_recur()\n" INDENT_END));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    STACK_GROW( L, 6);
    // slot _i contains a table where we search for functions (or a full userdata with a metatable)
    STACK_CHECK_START_REL(L, 0);                                                              // ... {_i}

    // if object is a userdata, replace it by its metatable
    if( lua_type( L, _i) == LUA_TUSERDATA)
    {
        lua_getmetatable( L, _i);                                                             // ... {_i} mt
        lua_replace( L, _i);                                                                  // ... {_i}
    }

    // if table is already visited, we are done
    lua_pushvalue( L, _i);                                                                    // ... {_i} {}
    lua_rawget( L, cache);                                                                    // ... {_i} nil|n
    visit_count = lua_tointeger( L, -1); // 0 if nil, else n
    lua_pop( L, 1);                                                                           // ... {_i}
    STACK_CHECK( L, 0);
    if( visit_count > 0)
    {
        DEBUGSPEW_CODE(fprintf( stderr, INDENT_BEGIN "already visited\n" INDENT_END));
        return;
    }

    // remember we visited this table (1-visit count)
    lua_pushvalue( L, _i);                                                                    // ... {_i} {}
    lua_pushinteger( L, visit_count + 1);                                                     // ... {_i} {} 1
    lua_rawset( L, cache);                                                                    // ... {_i}
    STACK_CHECK( L, 0);

    // this table is at breadth_first_cache index
    lua_newtable( L);                                                                         // ... {_i} {bfc}
    ASSERT_L( lua_gettop( L) == breadth_first_cache);
    // iterate over all entries in the processed table
    lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
    while( lua_next( L, _i) != 0)                                                             // ... {_i} {bfc} k v
    {
        // just for debug, not actually needed
        //char const* key = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : "not a string";
        // subtable: process it recursively
        if( lua_istable( L, -1))                                                                // ... {_i} {bfc} k {}
        {
            // increment visit count to make sure we will actually scan it at this recursive level
            lua_pushvalue( L, -1);                                                                // ... {_i} {bfc} k {} {}
            lua_pushvalue( L, -1);                                                                // ... {_i} {bfc} k {} {} {}
            lua_rawget( L, cache);                                                                // ... {_i} {bfc} k {} {} n?
            visit_count = lua_tointeger( L, -1) + 1; // 1 if we got nil, else n+1
            lua_pop( L, 1);                                                                       // ... {_i} {bfc} k {} {}
            lua_pushinteger( L, visit_count);                                                     // ... {_i} {bfc} k {} {} n
            lua_rawset( L, cache);                                                                // ... {_i} {bfc} k {}
            // store the table in the breadth-first cache
            lua_pushvalue( L, -2);                                                                // ... {_i} {bfc} k {} k
            lua_pushvalue( L, -2);                                                                // ... {_i} {bfc} k {} k {}
            lua_rawset( L, breadth_first_cache);                                                  // ... {_i} {bfc} k {}
            // generate a name, and if we already had one name, keep whichever is the shorter
            update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, _depth);                 // ... {_i} {bfc} k
        }
        else if( lua_isfunction( L, -1) && (luaG_getfuncsubtype( L, -1) != FST_Bytecode))       // ... {_i} {bfc} k func
        {
            // generate a name, and if we already had one name, keep whichever is the shorter
            update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, _depth);                 // ... {_i} {bfc} k
        }
        else
        {
            lua_pop( L, 1);                                                                       // ... {_i} {bfc} k
        }
        STACK_CHECK( L, 2);
    }
    // now process the tables we encountered at that depth
    ++ _depth;
    lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
    while( lua_next( L, breadth_first_cache) != 0)                                            // ... {_i} {bfc} k {}
    {
        DEBUGSPEW_CODE( char const* key = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : "not a string");
        DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "table '%s'\n" INDENT_END, key));
        DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
        // un-visit this table in case we do need to process it
        lua_pushvalue( L, -1);                                                                  // ... {_i} {bfc} k {} {}
        lua_rawget( L, cache);                                                                  // ... {_i} {bfc} k {} n
        ASSERT_L( lua_type( L, -1) == LUA_TNUMBER);
        visit_count = lua_tointeger( L, -1) - 1;
        lua_pop( L, 1);                                                                         // ... {_i} {bfc} k {}
        lua_pushvalue( L, -1);                                                                  // ... {_i} {bfc} k {} {}
        if( visit_count > 0)
        {
            lua_pushinteger( L, visit_count);                                                     // ... {_i} {bfc} k {} {} n
        }
        else
        {
            lua_pushnil( L);                                                                      // ... {_i} {bfc} k {} {} nil
        }
        lua_rawset( L, cache);                                                                  // ... {_i} {bfc} k {}
        // push table name in fqn stack (note that concatenation will crash if name is a not string!)
        lua_pushvalue( L, -2);                                                                  // ... {_i} {bfc} k {} k
        lua_rawseti( L, fqn, _depth);                                                           // ... {_i} {bfc} k {}
        populate_func_lookup_table_recur( DEBUGSPEW_PARAM_COMMA( U) L, _ctx_base, lua_gettop( L), _depth);
        lua_pop( L, 1);                                                                         // ... {_i} {bfc} k
        STACK_CHECK( L, 2);
    }
    // remove table name from fqn stack
    lua_pushnil( L);                                                                          // ... {_i} {bfc} nil
    lua_rawseti( L, fqn, _depth);                                                             // ... {_i} {bfc}
    -- _depth;
    // we are done with our cache
    lua_pop( L, 1);                                                                           // ... {_i}
    STACK_CHECK( L, 0);
    // we are done                                                                            // ... {_i} {bfc}
}

// #################################################################################################

/*
 * create a "fully.qualified.name" <-> function equivalence database
 */
void populate_func_lookup_table(lua_State* L, int i_, char const* name_)
{
    int const ctx_base = lua_gettop(L) + 1;
    int const in_base = lua_absindex(L, i_);
    int start_depth = 0;
    DEBUGSPEW_CODE( Universe* U = universe_get( L));
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "%p: populate_func_lookup_table('%s')\n" INDENT_END, L, name_ ? name_ : "nullptr"));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
    STACK_GROW(L, 3);
    STACK_CHECK_START_REL(L, 0);
    LOOKUP_REGKEY.pushValue(L);                                                      // {}
    STACK_CHECK( L, 1);
    ASSERT_L( lua_istable( L, -1));
    if( lua_type( L, in_base) == LUA_TFUNCTION) // for example when a module is a simple function
    {
        name_ = name_ ? name_ : "nullptr";
        lua_pushvalue( L, in_base);                                                  // {} f
        lua_pushstring( L, name_);                                                   // {} f _name
        lua_rawset( L, -3);                                                          // {}
        lua_pushstring( L, name_);                                                   // {} _name
        lua_pushvalue( L, in_base);                                                  // {} _name f
        lua_rawset( L, -3);                                                          // {}
        lua_pop( L, 1);                                                              //
    }
    else if( lua_type( L, in_base) == LUA_TTABLE)
    {
        lua_newtable(L);                                                             // {} {fqn}
        if( name_)
        {
            STACK_CHECK( L, 2);
            lua_pushstring( L, name_);                                                 // {} {fqn} "name"
            // generate a name, and if we already had one name, keep whichever is the shorter
            lua_pushvalue( L, in_base);                                           // {} {fqn} "name" t
            update_lookup_entry( DEBUGSPEW_PARAM_COMMA( U) L, ctx_base, start_depth);  // {} {fqn} "name"
            // don't forget to store the name at the bottom of the fqn stack
            ++ start_depth;
            lua_rawseti( L, -2, start_depth);                                          // {} {fqn}
            STACK_CHECK( L, 2);
        }
        // retrieve the cache, create it if we haven't done it yet
        LOOKUPCACHE_REGKEY.pushValue(L);                                               // {} {fqn} {cache}?
        if( lua_isnil( L, -1))
        {
            lua_pop( L, 1);                                                            // {} {fqn}
            lua_newtable( L);                                                          // {} {fqn} {cache}
            LOOKUPCACHE_REGKEY.setValue(L, [](lua_State* L) { lua_pushvalue(L, -2); });
            STACK_CHECK( L, 3);
        }
        // process everything we find in that table, filling in lookup data for all functions and tables we see there
        populate_func_lookup_table_recur( DEBUGSPEW_PARAM_COMMA( U) L, ctx_base, in_base, start_depth);
        lua_pop( L, 3);
    }
    else
    {
        lua_pop( L, 1);                                                         //
        (void) luaL_error( L, "unsupported module type %s", lua_typename( L, lua_type( L, in_base)));
    }
    STACK_CHECK( L, 0);
}

// #################################################################################################

/*---=== Inter-state copying ===---*/

// crc64/we of string "REG_MTID" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey REG_MTID{ 0x2e68f9b4751584dcull };

/*
* Get a unique ID for metatable at [i].
*/
[[nodiscard]] static lua_Integer get_mt_id(Universe* U, lua_State* L, int i)
{
    lua_Integer id;

    i = lua_absindex( L, i);

    STACK_GROW( L, 3);

    STACK_CHECK_START_REL(L, 0);
    push_registry_subtable( L, REG_MTID);        // ... _R[REG_MTID]
    lua_pushvalue( L, i);                        // ... _R[REG_MTID] {mt}
    lua_rawget( L, -2);                          // ... _R[REG_MTID] mtk?

    id = lua_tointeger( L, -1); // 0 for nil
    lua_pop( L, 1);                              // ... _R[REG_MTID]
    STACK_CHECK( L, 1);

    if( id == 0)
    {
        id = U->next_mt_id.fetch_add(1, std::memory_order_relaxed);

        /* Create two-way references: id_uint <-> table
        */
        lua_pushvalue( L, i);                      // ... _R[REG_MTID] {mt}
        lua_pushinteger( L, id);                   // ... _R[REG_MTID] {mt} id
        lua_rawset( L, -3);                        // ... _R[REG_MTID]

        lua_pushinteger( L, id);                   // ... _R[REG_MTID] id
        lua_pushvalue( L, i);                      // ... _R[REG_MTID] id {mt}
        lua_rawset( L, -3);                        // ... _R[REG_MTID]
    }
    lua_pop( L, 1);                              // ...

    STACK_CHECK( L, 0);

    return id;
}

// #################################################################################################

// function sentinel used to transfer native functions from/to keeper states
[[nodiscard]] static int func_lookup_sentinel(lua_State* L)
{
    return luaL_error(L, "function lookup sentinel for %s, should never be called", lua_tostring(L, lua_upvalueindex(1)));
}

// #################################################################################################

// function sentinel used to transfer native table from/to keeper states
[[nodiscard]] static int table_lookup_sentinel(lua_State* L)
{
    return luaL_error(L, "table lookup sentinel for %s, should never be called", lua_tostring(L, lua_upvalueindex(1)));
}

// #################################################################################################

// function sentinel used to transfer cloned full userdata from/to keeper states
[[nodiscard]] static int userdata_clone_sentinel(lua_State* L)
{
    return luaL_error(L, "userdata clone sentinel for %s, should never be called", lua_tostring(L, lua_upvalueindex(1)));
}

// #################################################################################################

/*
 * retrieve the name of a function/table in the lookup database
 */
[[nodiscard]] static char const* find_lookup_name(lua_State* L, int i, LookupMode mode_, char const* upName_, size_t* len_)
{
    DEBUGSPEW_CODE( Universe* const U = universe_get( L));
    char const* fqn;
    ASSERT_L( lua_isfunction( L, i) || lua_istable( L, i));  // ... v ...
    STACK_CHECK_START_REL(L, 0);
    STACK_GROW( L, 3); // up to 3 slots are necessary on error
    if (mode_ == LookupMode::FromKeeper)
    {
        lua_CFunction f = lua_tocfunction( L, i); // should *always* be func_lookup_sentinel or table_lookup_sentinel!
        if( f == func_lookup_sentinel || f == table_lookup_sentinel || f == userdata_clone_sentinel)
        {
            lua_getupvalue( L, i, 1);                            // ... v ... "f.q.n"
        }
        else
        {
            // if this is not a sentinel, this is some user-created table we wanted to lookup
            ASSERT_L(nullptr == f && lua_istable(L, i));
            // push anything that will convert to nullptr string
            lua_pushnil( L);                                     // ... v ... nil
        }
    }
    else
    {
        // fetch the name from the source state's lookup table
        LOOKUP_REGKEY.pushValue(L);                            // ... v ... {}
        STACK_CHECK( L, 1);
        ASSERT_L( lua_istable( L, -1));
        lua_pushvalue( L, i);                                  // ... v ... {} v
        lua_rawget( L, -2);                                    // ... v ... {} "f.q.n"
    }
    fqn = lua_tolstring( L, -1, len_);
    DEBUGSPEW_CODE( fprintf( stderr, INDENT_BEGIN "function [C] %s \n" INDENT_END, fqn));
    // popping doesn't invalidate the pointer since this is an interned string gotten from the lookup database
    lua_pop( L, (mode_ == LookupMode::FromKeeper) ? 1 : 2);    // ... v ...
    STACK_CHECK( L, 0);
    if (nullptr == fqn && !lua_istable(L, i)) // raise an error if we try to send an unknown function (but not for tables)
    {
        char const *from, *typewhat, *what, *gotchaA, *gotchaB;
        // try to discover the name of the function we want to send
        lua_getglobal( L, "decoda_name");                      // ... v ... decoda_name
        from = lua_tostring( L, -1);
        lua_pushcfunction( L, luaG_nameof);                    // ... v ... decoda_name luaG_nameof
        lua_pushvalue( L, i);                                  // ... v ... decoda_name luaG_nameof t
        lua_call( L, 1, 2);                                    // ... v ... decoda_name "type" "name"|nil
        typewhat = (lua_type( L, -2) == LUA_TSTRING) ? lua_tostring( L, -2) : luaL_typename( L, -2);
        // second return value can be nil if the table was not found
        // probable reason: the function was removed from the source Lua state before Lanes was required.
        if( lua_isnil( L, -1))
        {
            gotchaA = " referenced by";
            gotchaB = "\n(did you remove it from the source Lua state before requiring Lanes?)";
            what = upName_;
        }
        else
        {
            gotchaA = "";
            gotchaB = "";
            what = (lua_type( L, -1) == LUA_TSTRING) ? lua_tostring( L, -1) : luaL_typename( L, -1);
        }
        (void) luaL_error( L, "%s%s '%s' not found in %s origin transfer database.%s", typewhat, gotchaA, what, from ? from : "main", gotchaB);
        *len_ = 0;
        return nullptr;
    }
    STACK_CHECK( L, 0);
    return fqn;
}

// #################################################################################################

/*
 * Push a looked-up table, or nothing if we found nothing
 */
[[nodiscard]] static bool lookup_table(Dest L2, Source L1, SourceIndex i_, LookupMode mode_, char const* upName_)
{
    // get the name of the table we want to send
    size_t len;
    char const* fqn = find_lookup_name(L1, i_, mode_, upName_, &len);
    if (nullptr == fqn) // name not found, it is some user-created table
    {
        return false;
    }
    // push the equivalent table in the destination's stack, retrieved from the lookup table
    STACK_CHECK_START_REL(L2, 0);                                // L                       // L2
    STACK_GROW( L2, 3); // up to 3 slots are necessary on error
    switch( mode_)
    {
        default: // shouldn't happen, in theory...
        luaL_error(L1, "internal error: unknown lookup mode"); // doesn't return
        break;

        case LookupMode::ToKeeper:
        // push a sentinel closure that holds the lookup name as upvalue
        lua_pushlstring(L2, fqn, len);                                                      // "f.q.n"
        lua_pushcclosure(L2, table_lookup_sentinel, 1);                                     // f
        break;

        case LookupMode::LaneBody:
        case LookupMode::FromKeeper:
        LOOKUP_REGKEY.pushValue(L2);                                                        // {}
        STACK_CHECK(L2, 1);
        _ASSERT_L(L1, lua_istable(L2, -1));
        lua_pushlstring(L2, fqn, len);                                                      // {} "f.q.n"
        lua_rawget(L2, -2);                                                                 // {} t
        // we accept destination lookup failures in the case of transfering the Lanes body function (this will result in the source table being cloned instead)
        // but not when we extract something out of a keeper, as there is nothing to clone!
        if (lua_isnil(L2, -1) && mode_ == LookupMode::LaneBody)
        {
            lua_pop(L2, 2);                                                                 //
            STACK_CHECK(L2, 0);
            return false;
        }
        else if (!lua_istable(L2, -1))
        {
            char const* from, *to;
            lua_getglobal(L1, "decoda_name");                    // ... t ... decoda_name
            from = lua_tostring(L1, -1);
            lua_pop(L1, 1);                                      // ... t ...
            lua_getglobal(L2, "decoda_name");                                               // {} t decoda_name
            to = lua_tostring(L2, -1);
            lua_pop(L2, 1);                                                                 // {} t
            // when mode_ == LookupMode::FromKeeper, L is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
            luaL_error(
                (mode_ == LookupMode::FromKeeper) ? L2 : L1
                , "INTERNAL ERROR IN %s: table '%s' not found in %s destination transfer database."
                , from ? from : "main"
                , fqn
                , to ? to : "main"
            ); // doesn't return
            return false;
        }
        lua_remove(L2, -2);                                                                 // t
        break;
    }
    STACK_CHECK( L2, 1);
    return true;
}

// #################################################################################################

/* 
 * Check if we've already copied the same table from 'L', and
 * reuse the old copy. This allows table upvalues shared by multiple
 * local functions to point to the same table, also in the target.
 *
 * Always pushes a table to 'L2'.
 *
 * Returns true if the table was cached (no need to fill it!); false if
 * it's a virgin.
 */
[[nodiscard]] static bool push_cached_table(Dest L2, CacheIndex L2_cache_i, Source L1, SourceIndex i)
{
    void const* p{ lua_topointer(L1, i) };

    _ASSERT_L(L1, L2_cache_i != 0);
    STACK_GROW(L2, 3);                                                                              // L2
    STACK_CHECK_START_REL(L2, 0);

    // We don't need to use the from state ('L1') in ID since the life span
    // is only for the duration of a copy (both states are locked).
    // push a light userdata uniquely representing the table
    lua_pushlightuserdata(L2, const_cast<void*>(p));                                                // ... p

    //fprintf(stderr, "<< ID: %s >>\n", lua_tostring(L2, -1));

    lua_rawget(L2, L2_cache_i);                                                                     // ... {cached|nil}
    bool const not_found_in_cache{ lua_isnil(L2, -1) };
    if (not_found_in_cache)
    {
        // create a new entry in the cache
        lua_pop(L2, 1);                                                                             // ...
        lua_newtable(L2);                                                                           // ... {}
        lua_pushlightuserdata(L2, const_cast<void*>(p));                                            // ... {} p
        lua_pushvalue(L2, -2);                                                                      // ... {} p {}
        lua_rawset(L2, L2_cache_i);                                                                 // ... {}
    }
    STACK_CHECK(L2, 1);
    _ASSERT_L(L1, lua_istable( L2, -1));
    return !not_found_in_cache;
}

// #################################################################################################

/*
 * Return some name helping to identify an object
 */
[[nodiscard]] static int discover_object_name_recur(lua_State* L, int shortest_, int depth_)
{
    int const what = 1;                                     // o "r" {c} {fqn} ... {?}
    int const result = 2;
    int const cache = 3;
    int const fqn = 4;
    // no need to scan this table if the name we will discover is longer than one we already know
    if (shortest_ <= depth_ + 1)
    {
        return shortest_;
    }
    STACK_GROW(L, 3);
    STACK_CHECK_START_REL(L, 0);
    // stack top contains the table to search in
    lua_pushvalue(L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
    lua_rawget(L, cache);                                  // o "r" {c} {fqn} ... {?} nil/1
    // if table is already visited, we are done
    if( !lua_isnil(L, -1))
    {
        lua_pop(L, 1);                                       // o "r" {c} {fqn} ... {?}
        return shortest_;
    }
    // examined table is not in the cache, add it now
    lua_pop(L, 1);                                         // o "r" {c} {fqn} ... {?}
    lua_pushvalue(L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
    lua_pushinteger(L, 1);                                 // o "r" {c} {fqn} ... {?} {?} 1
    lua_rawset(L, cache);                                  // o "r" {c} {fqn} ... {?}
    // scan table contents
    lua_pushnil(L);                                        // o "r" {c} {fqn} ... {?} nil
    while (lua_next(L, -2))                                // o "r" {c} {fqn} ... {?} k v
    {
        //char const *const strKey = (lua_type(L, -2) == LUA_TSTRING) ? lua_tostring(L, -2) : nullptr; // only for debugging
        //lua_Number const numKey = (lua_type(L, -2) == LUA_TNUMBER) ? lua_tonumber(L, -2) : -6666; // only for debugging
        STACK_CHECK(L, 2);
        // append key name to fqn stack
        ++ depth_;
        lua_pushvalue(L, -2);                                // o "r" {c} {fqn} ... {?} k v k
        lua_rawseti(L, fqn, depth_);                         // o "r" {c} {fqn} ... {?} k v
        if (lua_rawequal(L, -1, what)) // is it what we are looking for?
        {
            STACK_CHECK(L, 2);
            // update shortest name
            if( depth_ < shortest_)
            {
                shortest_ = depth_;
                std::ignore = luaG_pushFQN(L, fqn, depth_, nullptr); // o "r" {c} {fqn} ... {?} k v "fqn"
                lua_replace(L, result);                          // o "r" {c} {fqn} ... {?} k v
            }
            // no need to search further at this level
            lua_pop(L, 2);                                     // o "r" {c} {fqn} ... {?}
            STACK_CHECK(L, 0);
            break;
        }
        switch (lua_type(L, -1))                             // o "r" {c} {fqn} ... {?} k v
        {
            default: // nil, boolean, light userdata, number and string aren't identifiable
            break;

            case LUA_TTABLE:                                    // o "r" {c} {fqn} ... {?} k {}
            STACK_CHECK(L, 2);
            shortest_ = discover_object_name_recur(L, shortest_, depth_);
            // search in the table's metatable too
            if (lua_getmetatable(L, -1))                       // o "r" {c} {fqn} ... {?} k {} {mt}
            {
                if( lua_istable(L, -1))
                {
                    ++ depth_;
                    lua_pushliteral(L, "__metatable");             // o "r" {c} {fqn} ... {?} k {} {mt} "__metatable"
                    lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k {} {mt}
                    shortest_ = discover_object_name_recur(L, shortest_, depth_);
                    lua_pushnil(L);                                // o "r" {c} {fqn} ... {?} k {} {mt} nil
                    lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k {} {mt}
                    -- depth_;
                }
                lua_pop(L, 1);                                   // o "r" {c} {fqn} ... {?} k {}
            }
            STACK_CHECK(L, 2);
            break;

            case LUA_TTHREAD:                                   // o "r" {c} {fqn} ... {?} k T
            // TODO: explore the thread's stack frame looking for our culprit?
            break;

            case LUA_TUSERDATA:                                 // o "r" {c} {fqn} ... {?} k U
            STACK_CHECK(L, 2);
            // search in the object's metatable (some modules are built that way)
            if (lua_getmetatable(L, -1))                       // o "r" {c} {fqn} ... {?} k U {mt}
            {
                if (lua_istable(L, -1))
                {
                    ++ depth_;
                    lua_pushliteral(L, "__metatable");             // o "r" {c} {fqn} ... {?} k U {mt} "__metatable"
                    lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k U {mt}
                    shortest_ = discover_object_name_recur(L, shortest_, depth_);
                    lua_pushnil(L);                                // o "r" {c} {fqn} ... {?} k U {mt} nil
                    lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k U {mt}
                    -- depth_;
                }
                lua_pop(L, 1);                                   // o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L, 2);
            // search in the object's uservalues
            {
                int uvi = 1;
                while (lua_getiuservalue(L, -1, uvi) != LUA_TNONE) // o "r" {c} {fqn} ... {?} k U {u}
                {
                    if( lua_istable(L, -1)) // if it is a table, look inside
                    {
                        ++ depth_;
                        lua_pushliteral(L, "uservalue");               // o "r" {c} {fqn} ... {?} k v {u} "uservalue"
                        lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k v {u}
                        shortest_ = discover_object_name_recur(L, shortest_, depth_);
                        lua_pushnil(L);                                // o "r" {c} {fqn} ... {?} k v {u} nil
                        lua_rawseti(L, fqn, depth_);                   // o "r" {c} {fqn} ... {?} k v {u}
                        -- depth_;
                    }
                    lua_pop(L, 1);                                   // o "r" {c} {fqn} ... {?} k U
                    ++ uvi;
                }
                // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
                lua_pop(L, 1);                                     // o "r" {c} {fqn} ... {?} k U
            }
            STACK_CHECK(L, 2);
            break;
        }
        // make ready for next iteration
        lua_pop(L, 1);                                       // o "r" {c} {fqn} ... {?} k
        // remove name from fqn stack
        lua_pushnil(L);                                      // o "r" {c} {fqn} ... {?} k nil
        lua_rawseti(L, fqn, depth_);                         // o "r" {c} {fqn} ... {?} k
        STACK_CHECK(L, 1);
        -- depth_;
    }                                                       // o "r" {c} {fqn} ... {?}
    STACK_CHECK(L, 0);
    // remove the visited table from the cache, in case a shorter path to the searched object exists
    lua_pushvalue(L, -1);                                  // o "r" {c} {fqn} ... {?} {?}
    lua_pushnil(L);                                        // o "r" {c} {fqn} ... {?} {?} nil
    lua_rawset(L, cache);                                  // o "r" {c} {fqn} ... {?}
    STACK_CHECK(L, 0);
    return shortest_;
}

// #################################################################################################

/*
 * "type", "name" = lanes.nameof( o)
 */
int luaG_nameof( lua_State* L)
{
    int what = lua_gettop( L);
    if( what > 1)
    {
        luaL_argerror( L, what, "too many arguments.");
    }

    // nil, boolean, light userdata, number and string aren't identifiable
    if( lua_type( L, 1) < LUA_TTABLE)
    {
        lua_pushstring( L, luaL_typename( L, 1));             // o "type"
        lua_insert( L, -2);                                   // "type" o
        return 2;
    }

    STACK_GROW( L, 4);
    STACK_CHECK_START_REL(L, 0);
    // this slot will contain the shortest name we found when we are done
    lua_pushnil( L);                                        // o nil
    // push a cache that will contain all already visited tables
    lua_newtable( L);                                       // o nil {c}
    // push a table whose contents are strings that, when concatenated, produce unique name
    lua_newtable( L);                                       // o nil {c} {fqn}
    lua_pushliteral( L, "_G");                              // o nil {c} {fqn} "_G"
    lua_rawseti( L, -2, 1);                                 // o nil {c} {fqn}
    // this is where we start the search
    lua_pushglobaltable( L);                                // o nil {c} {fqn} _G
    (void) discover_object_name_recur( L, 6666, 1);
    if( lua_isnil( L, 2)) // try again with registry, just in case...
    {
        lua_pop( L, 1);                                       // o nil {c} {fqn}
        lua_pushliteral( L, "_R");                            // o nil {c} {fqn} "_R"
        lua_rawseti( L, -2, 1);                               // o nil {c} {fqn}
        lua_pushvalue( L, LUA_REGISTRYINDEX);                 // o nil {c} {fqn} _R
        (void) discover_object_name_recur( L, 6666, 1);
    }
    lua_pop( L, 3);                                         // o "result"
    STACK_CHECK( L, 1);
    lua_pushstring( L, luaL_typename( L, 1));               // o "result" "type"
    lua_replace( L, -3);                                    // "type" "result"
    return 2;
}

// #################################################################################################

/*
 * Push a looked-up native/LuaJIT function.
 */
static void lookup_native_func(lua_State* L2, lua_State* L, int i, LookupMode mode_, char const* upName_)
{
    // get the name of the function we want to send
    size_t len;
    char const* fqn = find_lookup_name( L, i, mode_, upName_, &len);
    // push the equivalent function in the destination's stack, retrieved from the lookup table
    STACK_CHECK_START_REL(L2, 0);                                // L                        // L2
    STACK_GROW( L2, 3); // up to 3 slots are necessary on error
    switch( mode_)
    {
        default: // shouldn't happen, in theory...
        (void) luaL_error( L, "internal error: unknown lookup mode");
        return;

        case LookupMode::ToKeeper:
        // push a sentinel closure that holds the lookup name as upvalue
        lua_pushlstring( L2, fqn, len);                                                      // "f.q.n"
        lua_pushcclosure( L2, func_lookup_sentinel, 1);                                      // f
        break;

        case LookupMode::LaneBody:
        case LookupMode::FromKeeper:
        LOOKUP_REGKEY.pushValue(L2);                                                         // {}
        STACK_CHECK( L2, 1);
        ASSERT_L( lua_istable( L2, -1));
        lua_pushlstring( L2, fqn, len);                                                      // {} "f.q.n"
        lua_rawget( L2, -2);                                                                 // {} f
        // nil means we don't know how to transfer stuff: user should do something
        // anything other than function or table should not happen!
        if( !lua_isfunction( L2, -1) && !lua_istable( L2, -1))
        {
            char const* from, * to;
            lua_getglobal( L, "decoda_name");                    // ... f ... decoda_name
            from = lua_tostring( L, -1);
            lua_pop( L, 1);                                      // ... f ...
            lua_getglobal( L2, "decoda_name");                                                 // {} f decoda_name
            to = lua_tostring( L2, -1);
            lua_pop( L2, 1);                                                                   // {} f
            // when mode_ == LookupMode::FromKeeper, L is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
            (void) luaL_error(
                (mode_ == LookupMode::FromKeeper) ? L2 : L
                , "%s%s: function '%s' not found in %s destination transfer database."
                , lua_isnil( L2, -1) ? "" : "INTERNAL ERROR IN "
                , from ? from : "main"
                , fqn
                , to ? to : "main"
            );
            return;
        }
        lua_remove( L2, -2);                                                                 // f
        break;

        /* keep it in case I need it someday, who knows...
        case LookupMode::RawFunctions:
        {
            int n;
            char const* upname;
            lua_CFunction f = lua_tocfunction( L, i);
            // copy upvalues
            for( n = 0; (upname = lua_getupvalue( L, i, 1 + n)) != nullptr; ++ n)
            {
                luaG_inter_move( U, L, L2, 1, mode_);                                            // [up[,up ...]]
            }
            lua_pushcclosure( L2, f, n);                                                       //
        }
        break;
        */
    }
    STACK_CHECK( L2, 1);
}

// #################################################################################################

/*
 * Copy a function over, which has not been found in the cache.
 * L2 has the cache key for this function at the top of the stack
*/

#if USE_DEBUG_SPEW()
static char const* lua_type_names[] =
{
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
static char const* vt_names[] =
{
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
[[nodiscard]] static int buf_writer(lua_State* L, void const* b, size_t size, void* ud)
{
    luaL_Buffer* B = (luaL_Buffer*) ud;
    if( !B->L)
    {
        luaL_buffinit( L, B);
    }
    luaL_addlstring( B, (char const*) b, size);
    return 0;
}

// #################################################################################################

void InterCopyContext::copy_func() const
{
    _ASSERT_L(L1, L2_cache_i != 0);                                                                    // ... {cache} ... p
    STACK_GROW(L1, 2);
    STACK_CHECK_START_REL(L1, 0);


    // 'lua_dump()' needs the function at top of stack
    // if already on top of the stack, no need to push again
    bool const needToPush{ L1_i != lua_gettop(L1) };
    if (needToPush)
    {
        lua_pushvalue(L1, L1_i);                                // ... f
    }

    //
    // "value returned is the error code returned by the last call 
    // to the writer" (and we only return 0)
    // not sure this could ever fail but for memory shortage reasons
    // last parameter is Lua 5.4-specific (no stripping)
    luaL_Buffer B;
    B.L = nullptr;
    if (lua504_dump(L1, buf_writer, &B, 0) != 0)
    {
        luaL_error(L1, "internal error: function dump failed."); // doesn't return
    }

    // pushes dumped string on 'L1'
    luaL_pushresult(&B);                                        // ... f b

    // if not pushed, no need to pop
    if (needToPush)
    {
        lua_remove(L1, -2);                                     // ... b
    }

    // transfer the bytecode, then the upvalues, to create a similar closure
    {
        char const* name = nullptr;

        #if LOG_FUNC_INFO
        // "To get information about a function you push it onto the 
        // stack and start the what string with the character '>'."
        //
        {
            lua_Debug ar;
            lua_pushvalue( L, i);                              // ... b f
            // fills 'name' 'namewhat' and 'linedefined', pops function
            lua_getinfo( L, ">nS", &ar);                       // ... b
            name = ar.namewhat;
            fprintf( stderr, INDENT_BEGIN "FNAME: %s @ %d\n", i, s_indent, ar.short_src, ar.linedefined);  // just gives nullptr
        }
        #endif // LOG_FUNC_INFO
        {
            size_t sz;
            char const* s = lua_tolstring(L1, -1, &sz);        // ... b
            _ASSERT_L(L1, s && sz);
            STACK_GROW(L2, 2);
            // Note: Line numbers seem to be taken precisely from the 
            //       original function. 'name' is not used since the chunk
            //       is precompiled (it seems...). 
            //
            // TBD: Can we get the function's original name through, as well?
            //
            if (luaL_loadbuffer(L2, s, sz, name) != 0)                                                // ... {cache} ... p function
            {
                // chunk is precompiled so only LUA_ERRMEM can happen
                // "Otherwise, it pushes an error message"
                //
                STACK_GROW(L1, 1);
                luaL_error(L1, "%s: %s", name, lua_tostring(L2, -1)); // doesn't return
            }
            // remove the dumped string
            lua_pop(L1, 1);                                    // ...
            // now set the cache as soon as we can.
            // this is necessary if one of the function's upvalues references it indirectly
            // we need to find it in the cache even if it isn't fully transfered yet
            lua_insert(L2, -2);                                                                        // ... {cache} ... function p
            lua_pushvalue(L2, -2);                                                                     // ... {cache} ... function p function
            // cache[p] = function
            lua_rawset(L2, L2_cache_i);                                                                // ... {cache} ... function
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
            lua_pushglobaltable(L1);                           // ... _G
#endif // LUA_VERSION_NUM
            for (n = 0; (c.name = lua_getupvalue(L1, L1_i, 1 + n)) != nullptr; ++n)
            {                                                  // ... _G up[n]
                DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "UPNAME[%d]: %s -> " INDENT_END, n, c.name));
#if LUA_VERSION_NUM >= 502
                if( lua_rawequal(L1, -1, -2)) // is the upvalue equal to the global table?
                {
                    DEBUGSPEW_CODE(fprintf(stderr, "pushing destination global scope\n"));
                    lua_pushglobaltable(L2);                                                           // ... {cache} ... function <upvalues>
                }
                else
#endif // LUA_VERSION_NUM
                {
                    DEBUGSPEW_CODE(fprintf(stderr, "copying value\n"));
                    c.L1_i = SourceIndex{ lua_gettop(L1) };
                    if (!c.inter_copy_one())                                                           // ... {cache} ... function <upvalues>
                    {
                        luaL_error(L1, "Cannot copy upvalue type '%s'", luaL_typename(L1, -1)); // doesn't return
                    }
                }
                lua_pop(L1, 1);                                // ... _G
            }
#if LUA_VERSION_NUM >= 502
            lua_pop(L1, 1);                                    // ...
#endif // LUA_VERSION_NUM
        }
        // L2: function + 'n' upvalues (>=0)

        STACK_CHECK(L1, 0);

        // Set upvalues (originally set to 'nil' by 'lua_load')
        {
            for (int const func_index{ lua_gettop(L2) - n }; n > 0; --n)
            {
                char const* rc{ lua_setupvalue(L2, func_index, n) };                                   // ... {cache} ... function
                //
                // "assigns the value at the top of the stack to the upvalue and returns its name.
                // It also pops the value from the stack."

                _ASSERT_L(L1, rc);      // not having enough slots?
            }
            // once all upvalues have been set we are left
            // with the function at the top of the stack                                               // ... {cache} ... function
        }
    }
    STACK_CHECK(L1, 0);
}

// #################################################################################################

/* 
 * Check if we've already copied the same function from 'L', and reuse the old
 * copy.
 *
 * Always pushes a function to 'L2'.
 */
void InterCopyContext::copy_cached_func() const
{
    FuncSubType funcSubType;
    std::ignore = luaG_tocfunction(L1, L1_i, &funcSubType); // nullptr for LuaJIT-fast && bytecode functions
    if (funcSubType == FST_Bytecode)
    {
        void* const aspointer = const_cast<void*>(lua_topointer(L1, L1_i));
        // TBD: Merge this and same code for tables
        _ASSERT_L(L1, L2_cache_i != 0);

        STACK_GROW(L2, 2);

        // L2_cache[id_str]= function
        //
        STACK_CHECK_START_REL(L2, 0);

        // We don't need to use the from state ('L') in ID since the life span
        // is only for the duration of a copy (both states are locked).
        //

        // push a light userdata uniquely representing the function
        lua_pushlightuserdata(L2, aspointer);                         // ... {cache} ... p

        //fprintf( stderr, "<< ID: %s >>\n", lua_tostring( L2, -1));

        lua_pushvalue(L2, -1);                                        // ... {cache} ... p p
        lua_rawget(L2, L2_cache_i);                                   // ... {cache} ... p function|nil|true

        if (lua_isnil(L2, -1)) // function is unknown
        {
            lua_pop(L2, 1);                                           // ... {cache} ... p

            // Set to 'true' for the duration of creation; need to find self-references
            // via upvalues
            //
            // pushes a copy of the func, stores a reference in the cache
            copy_func();                                              // ... {cache} ... function
        }
        else // found function in the cache
        {
            lua_remove(L2, -2);                                       // ... {cache} ... function
        }
        STACK_CHECK(L2, 1);
        _ASSERT_L(L1, lua_isfunction(L2, -1));
    }
    else // function is native/LuaJIT: no need to cache
    {
        lookup_native_func(L2, L1, L1_i, mode, name);                 // ... {cache} ... function
        // if the function was in fact a lookup sentinel, we can either get a function or a table here
        _ASSERT_L(L1, lua_isfunction(L2, -1) || lua_istable(L2, -1));
    }
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::push_cached_metatable() const
{
    STACK_CHECK_START_REL(L1, 0);
    if (!lua_getmetatable(L1, L1_i))                                        // ... mt
    {
        STACK_CHECK(L1, 0);
        return false;
    }
    STACK_CHECK(L1, 1);

    lua_Integer const mt_id{ get_mt_id(U, L1, -1) }; // Unique id for the metatable

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, 4);
    // do we already know this metatable?
    push_registry_subtable(L2, REG_MTID);                                                               // _R[REG_MTID]
    lua_pushinteger(L2, mt_id);                                                                         // _R[REG_MTID] id
    lua_rawget(L2, -2);                                                                                 // _R[REG_MTID] mt|nil
    STACK_CHECK(L2, 2);

    if (lua_isnil(L2, -1))
    {   // L2 did not know the metatable
        lua_pop(L2, 1);                                                                                 // _R[REG_MTID]
        InterCopyContext const c{ U, L2, L1, L2_cache_i, SourceIndex{ lua_gettop(L1) }, VT::METATABLE, mode, name };
        if (!c.inter_copy_one())                                                                        // _R[REG_MTID] mt?
        {
            luaL_error(L1, "Error copying a metatable"); // doesn't return
        }

        STACK_CHECK(L2, 2);                                                                             // _R[REG_MTID] mt
        // mt_id -> metatable
        lua_pushinteger(L2, mt_id);                                                                     // _R[REG_MTID] mt id
        lua_pushvalue(L2, -2);                                                                          // _R[REG_MTID] mt id mt
        lua_rawset(L2, -4);                                                                             // _R[REG_MTID] mt

        // metatable -> mt_id
        lua_pushvalue(L2, -1);                                                                          // _R[REG_MTID] mt mt
        lua_pushinteger(L2, mt_id);                                                                     // _R[REG_MTID] mt mt id
        lua_rawset(L2, -4);                                                                             // _R[REG_MTID] mt
        STACK_CHECK(L2, 2);
    }
    lua_remove(L2, -2);                                                                                 // mt

    lua_pop(L1, 1);                                                         // ...
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

void InterCopyContext::inter_copy_keyvaluepair() const
{
    SourceIndex const val_i{ lua_gettop(L1) };
    SourceIndex const key_i{ val_i - 1 };

    // Only basic key types are copied over; others ignored
    InterCopyContext c{ U, L2, L1, L2_cache_i, key_i, VT::KEY, mode, name };
    if (!c.inter_copy_one())
    {
        return;
        // we could raise an error instead of ignoring the table entry, like so:
        //luaL_error(L1, "Unable to copy %s key '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", name, luaL_typename(L1, key_i)); // doesn't return
        // maybe offer this possibility as a global configuration option, or a linda setting, or as a parameter of the call causing the transfer?
    }

    char* valPath{ nullptr };
    if( U->verboseErrors)
    {
        // for debug purposes, let's try to build a useful name
        if (lua_type(L1, key_i) == LUA_TSTRING)
        {
            char const* key{ lua_tostring(L1, key_i) };
            size_t const keyRawLen = lua_rawlen(L1, key_i);
            size_t const bufLen = strlen(name) + keyRawLen + 2;
            valPath = (char*) alloca( bufLen);
            sprintf( valPath, "%s.%*s", name, (int) keyRawLen, key);
            key = nullptr;
        }
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (lua_isinteger(L1, key_i))
        {
            lua_Integer const key{ lua_tointeger(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 32 + 3);
            sprintf(valPath, "%s[" LUA_INTEGER_FMT "]", name, key);
        }
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
        else if (lua_type(L1, key_i) == LUA_TNUMBER)
        {
            lua_Number const key{ lua_tonumber(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 32 + 3);
            sprintf(valPath, "%s[" LUA_NUMBER_FMT "]", name, key);
        }
        else if (lua_type( L1, key_i) == LUA_TLIGHTUSERDATA)
        {
            void* const key{ lua_touserdata(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 16 + 5);
            sprintf(valPath, "%s[U:%p]", name, key);
        }
        else if( lua_type( L1, key_i) == LUA_TBOOLEAN)
        {
            int const key{ lua_toboolean(L1, key_i) };
            valPath = (char*) alloca(strlen(name) + 8);
            sprintf(valPath, "%s[%s]", name, key ? "true" : "false");
        }
    }
    /*
    * Contents of metatables are copied with cache checking;
    * important to detect loops.
    */
    c.L1_i = SourceIndex{ val_i };
    c.name = valPath ? valPath : name;
    c.vt = VT::NORMAL;
    if (c.inter_copy_one())
    {
        _ASSERT_L(L1, lua_istable( L2, -3));
        lua_rawset(L2, -3);    // add to table (pops key & val)
    }
    else
    {
        luaL_error(L1, "Unable to copy %s entry '%s' because of value is of type '%s'", (vt == VT::NORMAL) ? "table" : "metatable", valPath, luaL_typename(L1, val_i));
    }
}

// #################################################################################################

/*
* The clone cache is a weak valued table listing all clones, indexed by their userdatapointer
* fnv164 of string "CLONABLES_CACHE_KEY" generated at https://www.pelock.com/products/hash-calculator
*/
static constexpr UniqueKey CLONABLES_CACHE_KEY{ 0xD04EE018B3DEE8F5ull };

[[nodiscard]] bool InterCopyContext::copyclone() const
{
    SourceIndex const L1_i{ lua_absindex(L1, this->L1_i) };
    void* const source{ lua_touserdata(L1, L1_i) };

    STACK_CHECK_START_REL(L1, 0);                                                // L1 (source)             // L2 (destination)
    STACK_CHECK_START_REL(L2, 0);

    // Check if the source was already cloned during this copy
    lua_pushlightuserdata(L2, source);                                                                      // ... source
    lua_rawget(L2, L2_cache_i);                                                                             // ... clone?
    if (!lua_isnil(L2, -1))
    {
        STACK_CHECK(L2, 1);
        return true;
    }
    else
    {
        lua_pop(L2, 1);                                                                                     // ...
    }
    STACK_CHECK(L2, 0);

    // no metatable? -> not clonable
    if( !lua_getmetatable(L1, L1_i))                                             // ... mt?
    {
        STACK_CHECK(L1, 0);
        return false;
    }

    // no __lanesclone? -> not clonable
    lua_getfield(L1, -1, "__lanesclone");                                        // ... mt __lanesclone?
    if( lua_isnil(L1, -1))
    {
        lua_pop(L1, 2);                                                          // ...
        STACK_CHECK(L1, 0);
        return false;
    }

    // we need to copy over the uservalues of the userdata as well
    {
        int const mt{ lua_absindex(L1, -2) };                                    // ... mt __lanesclone
        size_t const userdata_size{ lua_rawlen(L1, L1_i) };
        // extract all the uservalues, but don't transfer them yet
        int uvi = 0;
        while (lua_getiuservalue( L1, L1_i, ++ uvi) != LUA_TNONE) {}             // ... mt __lanesclone [uv]+ nil
        // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
        lua_pop(L1, 1);                                                          // ... mt __lanesclone [uv]+
        -- uvi;
        // create the clone userdata with the required number of uservalue slots
        void* clone = lua_newuserdatauv(L2, userdata_size, uvi);                                             // ... u
        // copy the metatable in the target state, and give it to the clone we put there
        InterCopyContext c{ U, L2, L1, L2_cache_i, SourceIndex{ mt }, VT::NORMAL, mode, name };
        if (c.inter_copy_one())                                                                              // ... u mt|sentinel
        {
            if( LookupMode::ToKeeper == mode)                                                                // ... u sentinel
            {
                _ASSERT_L(L1, lua_tocfunction(L2, -1) == table_lookup_sentinel);
                // we want to create a new closure with a 'clone sentinel' function, where the upvalues are the userdata and the metatable fqn
                lua_getupvalue(L2, -1, 1);                                                                   // ... u sentinel fqn
                lua_remove(L2, -2);                                                                          // ... u fqn
                lua_insert(L2, -2);                                                                          // ... fqn u
                lua_pushcclosure(L2, userdata_clone_sentinel, 2);                                            // ... userdata_clone_sentinel
            }
            else // from keeper or direct                                                                    // ... u mt
            {
                _ASSERT_L(L1, lua_istable(L2, -1));
                lua_setmetatable(L2, -2);                                                                    // ... u
            }
            STACK_CHECK(L2, 1);
        }
        else
        {
            luaL_error(L1, "Error copying a metatable"); // doesn't return
        }
        // first, add the entry in the cache (at this point it is either the actual userdata or the keeper sentinel
        lua_pushlightuserdata( L2, source);                                                                  // ... u source
        lua_pushvalue( L2, -2);                                                                              // ... u source u
        lua_rawset( L2, L2_cache_i);                                                                         // ... u
        // make sure we have the userdata now
        if (LookupMode::ToKeeper == mode)                                                                    // ... userdata_clone_sentinel
        {
            lua_getupvalue(L2, -1, 2);                                                                       // ... userdata_clone_sentinel u
        }
        // assign uservalues
        while (uvi > 0)
        {
            c.L1_i = SourceIndex{ lua_absindex(L1, -1) };
            if (!c.inter_copy_one())                                                                         // ... u uv
            {
                luaL_error(L1, "Cannot copy upvalue type '%s'", luaL_typename(L1, -1)); // doesn't return
            }
            lua_pop(L1, 1);                                                      // ... mt __lanesclone [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, -2, uvi);                                                                  // ... u
            -- uvi;
        }
        // when we are done, all uservalues are popped from the source stack, and we want only the single transferred value in the destination
        if (LookupMode::ToKeeper == mode)                                                                    // ... userdata_clone_sentinel u
        {
            lua_pop(L2, 1);                                                                                  // ... userdata_clone_sentinel
        }
        STACK_CHECK(L2, 1);
        STACK_CHECK(L1, 2);
        // call cloning function in source state to perform the actual memory cloning
        lua_pushlightuserdata(L1, clone);                                        // ... mt __lanesclone clone
        lua_pushlightuserdata(L1, source);                                       // ... mt __lanesclone clone source
        lua_pushinteger(L1, static_cast<lua_Integer>(userdata_size));            // ... mt __lanesclone clone source size
        lua_call(L1, 3, 0);                                                      // ... mt
        STACK_CHECK(L1, 1);
    }

    STACK_CHECK(L2, 1);
    lua_pop(L1, 1);                                                              // ...
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_userdata() const
{
    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    if (vt == VT::KEY)
    {
        return false;
    }

    // try clonable userdata first
    if( copyclone())
    {
        STACK_CHECK(L1, 0);
        STACK_CHECK(L2, 1);
        return true;
    }

    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 0);

    // Allow only deep userdata entities to be copied across
    DEBUGSPEW_CODE(fprintf(stderr, "USERDATA\n"));
    if (copydeep())
    {
        STACK_CHECK(L1, 0);
        STACK_CHECK(L2, 1);
        return true;
    }

    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 0);

    // Not a deep or clonable full userdata
    if (U->demoteFullUserdata) // attempt demotion to light userdata
    {
        void* const lud{ lua_touserdata(L1, L1_i) };
        lua_pushlightuserdata(L2, lud);
    }
    else // raise an error
    {
        luaL_error(L1, "can't copy non-deep full userdata across lanes"); // doesn't return
    }

    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_function() const
{
    if (vt == VT::KEY)
    {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);                                                 // L1 (source)                // L2 (destination)
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(fprintf(stderr, "FUNCTION %s\n", name));

    if (lua_tocfunction(L1, L1_i) == userdata_clone_sentinel) // we are actually copying a clonable full userdata from a keeper
    {
        // clone the full userdata again

        // let's see if we already restored this userdata
        lua_getupvalue(L1, L1_i, 2);                                              // ... u
        void* source = lua_touserdata(L1, -1);
        lua_pushlightuserdata(L2, source);                                                                      // ... source
        lua_rawget(L2, L2_cache_i);                                                                             // ... u?
        if (!lua_isnil(L2, -1))
        {
            lua_pop(L1, 1);                                                       // ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 1);
            return true;
        }
        lua_pop(L2, 1);                                                                                         // ...

        // this function has 2 upvalues: the fqn of its metatable, and the userdata itself
        bool const found{ lookup_table(L2, L1, L1_i, mode, name) };                                             // ... mt?
        if (!found)
        {
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
            while (lua_getiuservalue(L1, source_i, ++uvi) != LUA_TNONE) {}        // ... u uv
            // when lua_getiuservalue() returned LUA_TNONE, it pushed a nil. pop it now
            lua_pop(L1, 1);                                                       // ... u [uv]*
            --uvi;
            STACK_CHECK(L1, uvi + 1);
            // create the clone userdata with the required number of uservalue slots
            clone = lua_newuserdatauv(L2, userdata_size, uvi);                                                  // ... mt u
            // add it in the cache
            lua_pushlightuserdata(L2, source);                                                                  // ... mt u source
            lua_pushvalue(L2, -2);                                                                              // ... mt u source u
            lua_rawset(L2, L2_cache_i);                                                                         // ... mt u
            // set metatable
            lua_pushvalue(L2, -2);                                                                              // ... mt u mt
            lua_setmetatable(L2, -2);                                                                           // ... mt u
            // transfer and assign uservalues
            InterCopyContext c{ *this };
            while (uvi > 0)
            {
                c.L1_i = SourceIndex{ lua_absindex(L1, -1) };
                if (!c.inter_copy_one())                                                                        // ... mt u uv
                {
                    luaL_error(L1, "Cannot copy upvalue type '%s'", luaL_typename(L1, -1)); // doesn't return
                }
                lua_pop(L1, 1);                                                   // ... u [uv]*
                // this pops the value from the stack
                lua_setiuservalue(L2, -2, uvi);                                                                 // ... mt u
                -- uvi;
            }
            // when we are done, all uservalues are popped from the stack, we can pop the source as well
            lua_pop(L1, 1);                                                       // ...
            STACK_CHECK(L1, 0);
            STACK_CHECK(L2, 2);                                                                                 // ... mt u
        }
        // perform the custom cloning part
        lua_insert(L2, -2);                                                                                     // ... u mt
        // __lanesclone should always exist because we wouldn't be restoring data from a userdata_clone_sentinel closure to begin with
        lua_getfield(L2, -1, "__lanesclone");                                                                   // ... u mt __lanesclone
        lua_remove(L2, -2);                                                                                     // ... u __lanesclone
        lua_pushlightuserdata(L2, clone);                                                                       // ... u __lanesclone clone
        lua_pushlightuserdata(L2, source);                                                                      // ... u __lanesclone clone source
        lua_pushinteger(L2, userdata_size);                                                                     // ... u __lanesclone clone source size
        // clone:__lanesclone(dest, source, size)
        lua_call(L2, 3, 0);                                                                                     // ... u
    }
    else // regular function
    {
        DEBUGSPEW_CODE(fprintf( stderr, "FUNCTION %s\n", name));
        DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
        copy_cached_func();                                                                                     // ... f
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
    return true;
}

// #################################################################################################

[[nodiscard]] bool InterCopyContext::inter_copy_table() const
{
    if (vt == VT::KEY)
    {
        return false;
    }

    STACK_CHECK_START_REL(L1, 0);
    STACK_CHECK_START_REL(L2, 0);
    DEBUGSPEW_CODE(fprintf(stderr, "TABLE %s\n", name));

    /*
     * First, let's try to see if this table is special (aka is it some table that we registered in our lookup databases during module registration?)
     * Note that this table CAN be a module table, but we just didn't register it, in which case we'll send it through the table cloning mechanism
     */
    if (lookup_table(L2, L1, L1_i, mode, name))
    {
        _ASSERT_L(L1, lua_istable(L2, -1) || (lua_tocfunction(L2, -1) == table_lookup_sentinel)); // from lookup data. can also be table_lookup_sentinel if this is a table we know
        return true;
    }

    /* Check if we've already copied the same table from 'L' (during this transmission), and
    * reuse the old copy. This allows table upvalues shared by multiple
    * local functions to point to the same table, also in the target.
    * Also, this takes care of cyclic tables and multiple references
    * to the same subtable.
    *
    * Note: Even metatables need to go through this test; to detect
    *       loops such as those in required module tables (getmetatable(lanes).lanes == lanes)
    */
    if (push_cached_table(L2, L2_cache_i, L1, L1_i))
    {
        _ASSERT_L(L1, lua_istable(L2, -1));    // from cache
        return true;
    }
    _ASSERT_L(L1, lua_istable(L2, -1));

    STACK_GROW(L1, 2);
    STACK_GROW(L2, 2);

    lua_pushnil(L1); // start iteration
    while (lua_next(L1, L1_i))
    {
        // need a function to prevent overflowing the stack with verboseErrors-induced alloca()
        inter_copy_keyvaluepair();
        lua_pop(L1, 1);    // pop value (next round)
    }
    STACK_CHECK(L1, 0);
    STACK_CHECK(L2, 1);

    // Metatables are expected to be immutable, and copied only once.
    if (push_cached_metatable())                                                                          // ... t mt?
    {
        lua_setmetatable(L2, -2);                                                                         // ... t
    }
    STACK_CHECK(L2, 1);
    STACK_CHECK(L1, 0);
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
    static constexpr int pod_mask = (1 << LUA_TNIL) | (1 << LUA_TBOOLEAN) | (1 << LUA_TLIGHTUSERDATA) | (1 << LUA_TNUMBER) | (1 << LUA_TSTRING);
    STACK_GROW(L2, 1);
    STACK_CHECK_START_REL(L1, 0);                                                      // L1                          // L2
    STACK_CHECK_START_REL(L2, 0);                                                      // L1                          // L2

    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "inter_copy_one()\n" INDENT_END));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    LuaType val_type{ lua_type_as_enum(L1, L1_i) };
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "%s %s: " INDENT_END, lua_type_names[static_cast<int>(val_type)], vt_names[static_cast<int>(vt)]));

    // Non-POD can be skipped if its metatable contains { __lanesignore = true }
    if (((1 << static_cast<int>(val_type)) & pod_mask) == 0)
    {
        if (lua_getmetatable(L1, L1_i))                                                // ... mt
        {
            lua_getfield(L1, -1, "__lanesignore");                                     // ... mt ignore?
            if( lua_isboolean(L1, -1) && lua_toboolean(L1, -1))
            {
                DEBUGSPEW_CODE(fprintf( stderr, INDENT_BEGIN "__lanesignore -> LUA_TNIL\n" INDENT_END));
                val_type = LuaType::NIL;
            }
            lua_pop(L1, 2);                                                            // ...
        }
    }
    STACK_CHECK(L1, 0);

    /* Lets push nil to L2 if the object should be ignored */
    bool ret{ true };
    switch (val_type)
    {
        /* Basic types allowed both as values, and as table keys */

        case LuaType::BOOLEAN:
        {
            int const v{ lua_toboolean(L1, L1_i) };
            DEBUGSPEW_CODE( fprintf(stderr, "%s\n", v ? "true" : "false"));
            lua_pushboolean(L2, v);
        }
        break;

        case LuaType::NUMBER:
        /* LNUM patch support (keeping integer accuracy) */
#if defined LUA_LNUM || LUA_VERSION_NUM >= 503
        if( lua_isinteger(L1, L1_i))
        {
            lua_Integer const v{ lua_tointeger(L1, L1_i) };
            DEBUGSPEW_CODE(fprintf(stderr, LUA_INTEGER_FMT "\n", v));
            lua_pushinteger(L2, v);
            break;
        }
        else
#endif // defined LUA_LNUM || LUA_VERSION_NUM >= 503
        {
            lua_Number const v{ lua_tonumber(L1, L1_i) };
            DEBUGSPEW_CODE(fprintf(stderr, LUA_NUMBER_FMT "\n", v));
            lua_pushnumber(L2, v);
        }
        break;

        case LuaType::STRING:
        {
            size_t len;
            char const* const s{ lua_tolstring(L1, L1_i, &len) };
            DEBUGSPEW_CODE(fprintf(stderr, "'%s'\n", s));
            lua_pushlstring(L2, s, len);
        }
        break;

        case LuaType::LIGHTUSERDATA:
        {
            void* const p{ lua_touserdata(L1, L1_i) };
            DEBUGSPEW_CODE(fprintf(stderr, "%p\n", p));
            lua_pushlightuserdata(L2, p);
        }
        break;

        /* The following types are not allowed as table keys */

        case LuaType::USERDATA:
        ret = inter_copy_userdata();
        break;

        case LuaType::NIL:
        if (vt == VT::KEY)
        {
            ret = false;
            break;
        }
        lua_pushnil( L2);
        break;

        case LuaType::FUNCTION:
        ret = inter_copy_function();
        break;

        case LuaType::TABLE:
        ret = inter_copy_table();
        break;

        /* The following types cannot be copied */

        case LuaType::CDATA:
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
    _ASSERT_L(L1, vt == VT::NORMAL);

    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "InterCopyContext::inter_copy()\n" INDENT_END));
    DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });

    int const top_L1{ lua_gettop(L1) };
    if (n_ > top_L1)
    {
        // requesting to copy more than is available?
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "nothing to copy()\n" INDENT_END));
        return InterCopyResult::NotEnoughValues;
    }

    STACK_CHECK_START_REL(L2, 0);
    STACK_GROW(L2, n_ + 1);

    /*
    * Make a cache table for the duration of this copy. Collects tables and
    * function entries, avoiding the same entries to be passed on as multiple
    * copies. ESSENTIAL i.e. for handling upvalue tables in the right manner!
    */
    int const top_L2{ lua_gettop(L2) };                                                             // ...
    lua_newtable(L2);                                                                               // ... cache

    char tmpBuf[16];
    char const* const pBuf{ U->verboseErrors ? tmpBuf : "?" };
    InterCopyContext c{ U, L2, L1, CacheIndex{ top_L2 + 1 }, {}, VT::NORMAL, mode, pBuf };
    bool copyok{ true };
    STACK_CHECK_START_REL(L1, 0);
    for (int i{ top_L1 - n_ + 1 }, j{ 1 }; i <= top_L1; ++i, ++j)
    {
        if (U->verboseErrors)
        {
            sprintf(tmpBuf, "arg_%d", j);
        }
        c.L1_i = SourceIndex{ i };
        copyok = c.inter_copy_one();                                                                // ... cache {}n
        if (!copyok)
        {
            break;
        }
    }
    STACK_CHECK(L1, 0);

    if (copyok)
    {
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
    lua_pop( L1, n_);
    return ret;
}

// #################################################################################################

// transfers stuff from L1->_G["package"] to L2->_G["package"]
// returns InterCopyResult::Success if everything is fine
// returns InterCopyResult::Error if pushed an error message in L1
// else raise an error in L1
[[nodiscard]] InterCopyResult InterCopyContext::inter_copy_package() const
{
    DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "InterCopyContext::inter_copy_package()\n" INDENT_END));

    class OnExit
    {
        private:

        lua_State* const L2;
        int const top_L2;
        DEBUGSPEW_CODE(DebugSpewIndentScope m_scope);

        public:

        OnExit(Universe* U_, lua_State* L2_)
        : L2{ L2_ }
        , top_L2{ lua_gettop(L2) }
        DEBUGSPEW_COMMA_PARAM(m_scope{ U_ })
        {
        }

        ~OnExit()
        {
            lua_settop(L2, top_L2);
        }
    } onExit{ U, L2 };

    STACK_CHECK_START_REL(L1, 0);
    if (lua_type_as_enum(L1, L1_i) != LuaType::TABLE)
    {
        lua_pushfstring(L1, "expected package as table, got %s", luaL_typename(L1, L1_i));
        STACK_CHECK(L1, 1);
        // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
        if (mode == LookupMode::LaneBody)
        {
            lua_error(L1); // doesn't return
        }
        return InterCopyResult::Error;
    }
    lua_getglobal(L2, "package"); // TODO: use _R._LOADED.package instead of _G.package
    if (lua_isnil(L2, -1)) // package library not loaded: do nothing
    {
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "'package' not loaded, nothing to do\n" INDENT_END));
        STACK_CHECK(L1, 0);
        return InterCopyResult::Success;
    }

    InterCopyResult result{ InterCopyResult::Success };
    // package.loaders is renamed package.searchers in Lua 5.2
    // but don't copy it anyway, as the function names change depending on the slot index!
    // users should provide an on_state_create function to setup custom loaders instead
    // don't copy package.preload in keeper states (they don't know how to translate functions)
    char const* entries[] = { "path", "cpath", (mode == LookupMode::LaneBody) ? "preload" : nullptr /*, (LUA_VERSION_NUM == 501) ? "loaders" : "searchers"*/, nullptr };
    for (char const* const entry : entries)
    {
        if (!entry)
        {
            continue;
        }
        DEBUGSPEW_CODE(fprintf(stderr, INDENT_BEGIN "package.%s\n" INDENT_END, entry));
        lua_getfield(L1, L1_i, entry);
        if (lua_isnil(L1, -1))
        {
            lua_pop(L1, 1);
        }
        else
        {
            {
                DEBUGSPEW_CODE(DebugSpewIndentScope scope{ U });
                result = inter_move(1); // moves the entry to L2
                STACK_CHECK(L1, 0);
            }
            if (result == InterCopyResult::Success)
            {
                lua_setfield(L2, -2, entry); // set package[entry]
            }
            else
            {
                lua_pushfstring(L1, "failed to copy package entry %s", entry);
                // raise the error when copying from lane to lane, else just leave it on the stack to be raised later
                if (mode == LookupMode::LaneBody)
                {
                    lua_error(L1); // doesn't return
                }
                lua_pop(L1, 1);
                break;
            }
        }
    }
    STACK_CHECK(L1, 0);
    return result;
}
