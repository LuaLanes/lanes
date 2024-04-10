/*
 * DEEP.CPP                         Copyright (c) 2024, Benoit Germain
 *
 * Deep userdata support, separate in its own source file to help integration
 * without enforcing a Lanes dependency
 */

/*
===============================================================================

Copyright (C) 2002-10 Asko Kauppi <akauppi@gmail.com>
              2011-24 Benoit Germain <bnt.germain@gmail.com>

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

#include "deep.h"

#include "compat.h"
#include "tools.h"
#include "uniquekey.h"
#include "universe.h"

#include <bit>
#include <cassert>

/*-- Metatable copying --*/

/*---=== Deep userdata ===---*/

/* 
* 'registry[REGKEY]' is a two-way lookup table for 'idfunc's and those type's
* metatables:
*
*   metatable   ->  idfunc
*   idfunc      ->  metatable
*/
// crc64/we of string "DEEP_LOOKUP_KEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey DEEP_LOOKUP_KEY{ 0x9fb9b4f3f633d83dull };

/*
 * The deep proxy cache is a weak valued table listing all deep UD proxies indexed by the deep UD that they are proxying
 * crc64/we of string "DEEP_PROXY_CACHE_KEY" generated at http://www.nitrxgen.net/hashgen/
*/
static constexpr UniqueKey DEEP_PROXY_CACHE_KEY{ 0x05773d6fc26be106ull };

/*
* Sets up [-1]<->[-2] two-way lookups, and ensures the lookup table exists.
* Pops the both values off the stack.
*/
static void set_deep_lookup(lua_State* L)
{
    STACK_GROW( L, 3);
    STACK_CHECK_START_REL(L, 2);                             // a b
    push_registry_subtable( L, DEEP_LOOKUP_KEY);             // a b {}
    STACK_CHECK( L, 3);
    lua_insert( L, -3);                                      // {} a b
    lua_pushvalue( L, -1);                                   // {} a b b
    lua_pushvalue( L,-3);                                    // {} a b b a
    lua_rawset( L, -5);                                      // {} a b
    lua_rawset( L, -3);                                      // {}
    lua_pop( L, 1);                                          //
    STACK_CHECK( L, 0);
}

// ################################################################################################

/*
* Pops the key (metatable or idfunc) off the stack, and replaces with the
* deep lookup value (idfunc/metatable/nil).
*/
static void get_deep_lookup(lua_State* L)
{
    STACK_GROW( L, 1);
    STACK_CHECK_START_REL(L, 1);                             // a
    DEEP_LOOKUP_KEY.pushValue(L);                            // a {}
    if( !lua_isnil( L, -1))
    {
        lua_insert( L, -2);                                  // {} a
        lua_rawget( L, -2);                                  // {} b
    }
    lua_remove( L, -2);                                      // a|b
    STACK_CHECK( L, 1);
}

// ################################################################################################

/*
* Return the registered ID function for 'index' (deep userdata proxy),
* or nullptr if 'index' is not a deep userdata proxy.
*/
[[nodiscard]] static inline luaG_IdFunction get_idfunc(lua_State* L, int index, LookupMode mode_)
{
    // when looking inside a keeper, we are 100% sure the object is a deep userdata
    if (mode_ == LookupMode::FromKeeper)
    {
        DeepPrelude** const proxy{ lua_tofulluserdata<DeepPrelude*>(L, index) };
        // we can (and must) cast and fetch the internally stored idfunc
        return (*proxy)->idfunc;
    }
    else
    {
        // essentially we are making sure that the metatable of the object we want to copy is stored in our metatable/idfunc database
        // it is the only way to ensure that the userdata is indeed a deep userdata!
        // of course, we could just trust the caller, but we won't
        STACK_GROW( L, 1);
        STACK_CHECK_START_REL(L, 0);

        if( !lua_getmetatable( L, index))       // deep ... metatable?
        {
            return nullptr; // no metatable: can't be a deep userdata object!
        }

        // replace metatable with the idfunc pointer, if it is actually a deep userdata
        get_deep_lookup( L);                    // deep ... idfunc|nil

        luaG_IdFunction const ret{ *lua_tolightuserdata<luaG_IdFunction>(L, -1) }; // nullptr if not a userdata
        lua_pop( L, 1);
        STACK_CHECK( L, 0);
        return ret;
    }
}

// ################################################################################################

void free_deep_prelude(lua_State* L, DeepPrelude* prelude_)
{
    ASSERT_L(prelude_->idfunc);
    STACK_CHECK_START_REL(L, 0);
    // Call 'idfunc( "delete", deep_ptr )' to make deep cleanup
    lua_pushlightuserdata( L, prelude_);
    prelude_->idfunc( L, DeepOp::Delete);
    lua_pop(L, 1);
    STACK_CHECK(L, 0);
}

// ################################################################################################

/*
 * void= mt.__gc( proxy_ud )
 *
 * End of life for a proxy object; reduce the deep reference count and clean it up if reaches 0.
 *
 */
[[nodiscard]] static int deep_userdata_gc(lua_State* L)
{
    DeepPrelude** const proxy{ lua_tofulluserdata<DeepPrelude*>(L, 1) };
    DeepPrelude* p = *proxy;

    // can work without a universe if creating a deep userdata from some external C module when Lanes isn't loaded
    // in that case, we are not multithreaded and locking isn't necessary anyway
    bool const isLastRef{ p->m_refcount.fetch_sub(1, std::memory_order_relaxed) == 1 };

    if (isLastRef)
    {
        // retrieve wrapped __gc
        lua_pushvalue( L, lua_upvalueindex( 1));                              // self __gc?
        if( !lua_isnil( L, -1))
        {
            lua_insert( L, -2);                                               // __gc self
            lua_call( L, 1, 0);                                               //
        }
        // 'idfunc' expects a clean stack to work on
        lua_settop( L, 0);
        free_deep_prelude( L, p);

        // top was set to 0, then userdata was pushed. "delete" might want to pop the userdata (we don't care), but should not push anything!
        if ( lua_gettop( L) > 1)
        {
            return luaL_error( L, "Bad idfunc(DeepOp::Delete): should not push anything");
        }
    }
    *proxy = nullptr; // make sure we don't use it any more, just in case
    return 0;
}

// ################################################################################################

/*
 * Push a proxy userdata on the stack.
 * returns nullptr if ok, else some error string related to bad idfunc behavior or module require problem
 * (error cannot happen with mode_ == LookupMode::ToKeeper)
 *
 * Initializes necessary structures if it's the first time 'idfunc' is being
 * used in this Lua state (metatable, registring it). Otherwise, increments the
 * reference count.
 */
char const* push_deep_proxy(Dest L, DeepPrelude* prelude, int nuv_, LookupMode mode_)
{
    // Check if a proxy already exists
    push_registry_subtable_mode( L, DEEP_PROXY_CACHE_KEY, "v");                                        // DPC
    lua_pushlightuserdata( L, prelude);                                                                // DPC deep
    lua_rawget( L, -2);                                                                                // DPC proxy
    if ( !lua_isnil( L, -1))
    {
        lua_remove( L, -2);                                                                            // proxy
        return nullptr;
    }
    else
    {
        lua_pop( L, 1);                                                                                // DPC
    }

    STACK_GROW( L, 7);
    STACK_CHECK_START_REL(L, 0);

    // a new full userdata, fitted with the specified number of uservalue slots (always 1 for Lua < 5.4)
    DeepPrelude** proxy = (DeepPrelude**) lua_newuserdatauv(L, sizeof(DeepPrelude*), nuv_);            // DPC proxy
    ASSERT_L( proxy);
    *proxy = prelude;
    prelude->m_refcount.fetch_add(1, std::memory_order_relaxed); // one more proxy pointing to this deep data

    // Get/create metatable for 'idfunc' (in this state)
    lua_pushlightuserdata( L, std::bit_cast<void*>(prelude->idfunc));                                  // DPC proxy idfunc
    get_deep_lookup( L);                                                                               // DPC proxy metatable?

    if( lua_isnil( L, -1)) // // No metatable yet.
    {
        char const* modname;
        int oldtop = lua_gettop( L);                                                                   // DPC proxy nil
        lua_pop( L, 1);                                                                                // DPC proxy
        // 1 - make one and register it
        if (mode_ != LookupMode::ToKeeper)
        {
            (void) prelude->idfunc( L, DeepOp::Metatable);                                                 // DPC proxy metatable
            if( lua_gettop( L) - oldtop != 0 || !lua_istable( L, -1))
            {
                lua_settop( L, oldtop);                                                                // DPC proxy X
                lua_pop( L, 3);                                                                        //
                return "Bad idfunc(eOP_metatable): unexpected pushed value";
            }
            // if the metatable contains a __gc, we will call it from our own
            lua_getfield( L, -1, "__gc");                                                              // DPC proxy metatable __gc
        }
        else
        {
            // keepers need a minimal metatable that only contains our own __gc
            lua_newtable( L);                                                                          // DPC proxy metatable
            lua_pushnil( L);                                                                           // DPC proxy metatable nil
        }
        if( lua_isnil( L, -1))
        {
            // Add our own '__gc' method
            lua_pop( L, 1);                                                                            // DPC proxy metatable
            lua_pushcfunction( L, deep_userdata_gc);                                                   // DPC proxy metatable deep_userdata_gc
        }
        else
        {
            // Add our own '__gc' method wrapping the original
            lua_pushcclosure( L, deep_userdata_gc, 1);                                                 // DPC proxy metatable deep_userdata_gc
        }
        lua_setfield( L, -2, "__gc");                                                                  // DPC proxy metatable

        // Memorize for later rounds
        lua_pushvalue( L, -1);                                                                         // DPC proxy metatable metatable
        lua_pushlightuserdata( L, std::bit_cast<void*>(prelude->idfunc));                              // DPC proxy metatable metatable idfunc
        set_deep_lookup( L);                                                                           // DPC proxy metatable

        // 2 - cause the target state to require the module that exported the idfunc
        // this is needed because we must make sure the shared library is still loaded as long as we hold a pointer on the idfunc
        {
            int oldtop_module = lua_gettop( L);
            modname = (char const*) prelude->idfunc( L, DeepOp::Module);                               // DPC proxy metatable
            // make sure the function pushed nothing on the stack!
            if( lua_gettop( L) - oldtop_module != 0)
            {
                lua_pop( L, 3);                                                                        //
                return "Bad idfunc(eOP_module): should not push anything";
            }
        }
        if (nullptr != modname) // we actually got a module name
        {
            // L.registry._LOADED exists without having registered the 'package' library.
            lua_getglobal( L, "require");                                                              // DPC proxy metatable require()
            // check that the module is already loaded (or being loaded, we are happy either way)
            if( lua_isfunction( L, -1))
            {
                lua_pushstring( L, modname);                                                           // DPC proxy metatable require() "module"
                lua_getfield( L, LUA_REGISTRYINDEX, LUA_LOADED_TABLE);                                 // DPC proxy metatable require() "module" _R._LOADED
                if( lua_istable( L, -1))
                {
                    lua_pushvalue( L, -2);                                                             // DPC proxy metatable require() "module" _R._LOADED "module"
                    lua_rawget( L, -2);                                                                // DPC proxy metatable require() "module" _R._LOADED module
                    int const alreadyloaded = lua_toboolean( L, -1);
                    if( !alreadyloaded) // not loaded
                    {
                        int require_result;
                        lua_pop( L, 2);                                                                // DPC proxy metatable require() "module"
                        // require "modname"
                        require_result = lua_pcall( L, 1, 0, 0);                                       // DPC proxy metatable error?
                        if( require_result != LUA_OK)
                        {
                            // failed, return the error message
                            lua_pushfstring( L, "error while requiring '%s' identified by idfunc(eOP_module): ", modname);
                            lua_insert( L, -2);                                                        // DPC proxy metatable prefix error
                            lua_concat( L, 2);                                                         // DPC proxy metatable error
                            return lua_tostring( L, -1);
                        }
                    }
                    else // already loaded, we are happy
                    {
                        lua_pop( L, 4);                                                                // DPC proxy metatable
                    }
                }
                else // no L.registry._LOADED; can this ever happen?
                {
                    lua_pop( L, 6);                                                                    //
                    return "unexpected error while requiring a module identified by idfunc(eOP_module)";
                }
            }
            else // a module name, but no require() function :-(
            {
                lua_pop( L, 4);                                                                        //
                return "lanes receiving deep userdata should register the 'package' library";
            }
        }
    }
    STACK_CHECK(L, 2);                                                                                 // DPC proxy metatable
    ASSERT_L(lua_type(L, -2) == LUA_TUSERDATA);
    ASSERT_L(lua_istable( L, -1));
    lua_setmetatable( L, -2);                                                                          // DPC proxy

    // If we're here, we obviously had to create a new proxy, so cache it.
    lua_pushlightuserdata( L, prelude);                                                                // DPC proxy deep
    lua_pushvalue( L, -2);                                                                             // DPC proxy deep proxy
    lua_rawset( L, -4);                                                                                // DPC proxy
    lua_remove( L, -2);                                                                                // proxy
    ASSERT_L(lua_type(L, -1) == LUA_TUSERDATA);
    STACK_CHECK(L, 0);
    return nullptr;
}

// ################################################################################################

/*
* Create a deep userdata
*
*   proxy_ud= deep_userdata( idfunc [, ...] )
*
* Creates a deep userdata entry of the type defined by 'idfunc'.
* Parameters found on the stack are left as is passed on to the 'idfunc' "new" invocation.
*
* 'idfunc' must fulfill the following features:
*
*   lightuserdata = idfunc( DeepOp::New [, ...] )      -- creates a new deep data instance
*   void = idfunc( DeepOp::Delete, lightuserdata )     -- releases a deep data instance
*   tbl = idfunc( DeepOp::Metatable )                  -- gives metatable for userdata proxies
*
* Reference counting and true userdata proxying are taken care of for the
* actual data type.
*
* Types using the deep userdata system (and only those!) can be passed between
* separate Lua states via 'luaG_inter_move()'.
*
* Returns:  'proxy' userdata for accessing the deep data via 'luaG_todeep()'
*/
int luaG_newdeepuserdata(Dest L, luaG_IdFunction idfunc, int nuv_)
{
    STACK_GROW( L, 1);
    STACK_CHECK_START_REL(L, 0);
    int const oldtop{ lua_gettop(L) };
    DeepPrelude* const prelude{ static_cast<DeepPrelude*>(idfunc(L, DeepOp::New)) };
    if (prelude == nullptr)
    {
        return luaL_error( L, "idfunc(DeepOp::New) failed to create deep userdata (out of memory)");
    }

    if( prelude->magic != DEEP_VERSION)
    {
        // just in case, don't leak the newly allocated deep userdata object
        lua_pushlightuserdata( L, prelude);
        idfunc( L, DeepOp::Delete);
        return luaL_error( L, "Bad idfunc(DeepOp::New): DEEP_VERSION is incorrect, rebuild your implementation with the latest deep implementation");
    }

    ASSERT_L(prelude->m_refcount.load(std::memory_order_relaxed) == 0); // 'push_deep_proxy' will lift it to 1
    prelude->idfunc = idfunc;

    if( lua_gettop( L) - oldtop != 0)
    {
        // just in case, don't leak the newly allocated deep userdata object
        lua_pushlightuserdata( L, prelude);
        idfunc( L, DeepOp::Delete);
        return luaL_error( L, "Bad idfunc(DeepOp::New): should not push anything on the stack");
    }

    char const* const errmsg{ push_deep_proxy(L, prelude, nuv_, LookupMode::LaneBody) }; // proxy
    if (errmsg != nullptr)
    {
        return luaL_error( L, errmsg);
    }
    STACK_CHECK( L, 1);
    return 1;
}

// ################################################################################################

/*
* Access deep userdata through a proxy.
*
* Reference count is not changed, and access to the deep userdata is not
* serialized. It is the module's responsibility to prevent conflicting usage.
*/
DeepPrelude* luaG_todeep(lua_State* L, luaG_IdFunction idfunc, int index)
{
    STACK_CHECK_START_REL(L, 0);
    // ensure it is actually a deep userdata
    if (get_idfunc(L, index, LookupMode::LaneBody) != idfunc)
    {
        return nullptr; // no metatable, or wrong kind
    }
    STACK_CHECK(L, 0);

    DeepPrelude** const proxy{ lua_tofulluserdata<DeepPrelude*>(L, index) };
    return *proxy;
}

// ################################################################################################

/*
 * Copy deep userdata between two separate Lua states (from L to L2)
 *
 * Returns:
 *   the id function of the copied value, or nullptr for non-deep userdata
 *   (not copied)
 */
bool copydeep(Universe* U, Dest L2, int L2_cache_i, Source L, int i, LookupMode mode_, char const* upName_)
{
    luaG_IdFunction const idfunc { get_idfunc(L, i, mode_) };
    if (idfunc == nullptr)
    {
        return false;   // not a deep userdata
    }

    STACK_CHECK_START_REL(L, 0);
    STACK_CHECK_START_REL(L2, 0);

    // extract all uservalues of the source
    int nuv = 0;
    while (lua_getiuservalue(L, i, nuv + 1) != LUA_TNONE)                                // ... u [uv]* nil
    {
        ++ nuv;
    }
    // last call returned TNONE and pushed nil, that we don't need
    lua_pop( L, 1);                                                                      // ... u [uv]*
    STACK_CHECK( L, nuv);

    char const* errmsg{ push_deep_proxy(L2, *lua_tofulluserdata<DeepPrelude*>(L, i), nuv, mode_) };         // u

    // transfer all uservalues of the source in the destination
    {
        int const clone_i = lua_gettop( L2);
        while( nuv)
        {
            if (!inter_copy_one(U, L2, L2_cache_i, L, lua_absindex(L, -1), VT::NORMAL, mode_, upName_))     // u uv
            {
                return luaL_error(L, "Cannot copy upvalue type '%s'", luaL_typename(L, -1));
            }
            lua_pop( L, 1);                                                              // ... u [uv]*
            // this pops the value from the stack
            lua_setiuservalue(L2, clone_i, nuv);                                                            // u
            -- nuv;
        }
    }

    STACK_CHECK(L2, 1);
    STACK_CHECK(L, 0);

    if (errmsg != nullptr)
    {
        // raise the error in the proper state (not the keeper)
        lua_State* const errL{ (mode_ == LookupMode::FromKeeper) ? L2 : L };
        std::ignore = luaL_error(errL, errmsg); // doesn't return
    }
    return true;
}