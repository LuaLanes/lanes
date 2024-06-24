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
#include "_pch.h"
#include "deep.h"

#include "tools.h"

/*-- Metatable copying --*/

/*---=== Deep userdata ===---*/

/*
 * 'registry[REGKEY]' is a two-way lookup table for 'factory's and those type's
 * metatables:
 *
 *   metatable   ->  factory
 *   factory      ->  metatable
 */
// xxh64 of string "kDeepLookupRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kDeepLookupRegKey{ 0xC6788345703C6059ull };

/*
 * The deep proxy cache is a weak valued table listing all deep UD proxies indexed by the deep UD that they are proxying
 * xxh64 of string "kDeepProxyCacheRegKey" generated at https://www.pelock.com/products/hash-calculator
 */
static constexpr RegistryUniqueKey kDeepProxyCacheRegKey{ 0xEBCD49AE1A3DD35Eull };

// #################################################################################################
// #################################################################################################
namespace {
    // #############################################################################################
    // #############################################################################################

    /*
     * void= mt.__gc( proxy_ud )
     *
     * End of life for a proxy object; reduce the deep reference count and clean it up if reaches 0.
     *
     */
    [[nodiscard]] static int DeepGC(lua_State* const L_)
    {
        DeepPrelude* const* const _proxy{ luaG_tofulluserdata<DeepPrelude*>(L_, 1) };
        DeepPrelude* const _p{ *_proxy };

        // can work without a universe if creating a deep userdata from some external C module when Lanes isn't loaded
        // in that case, we are not multithreaded and locking isn't necessary anyway
        bool const _isLastRef{ _p->refcount.fetch_sub(1, std::memory_order_relaxed) == 1 };

        if (_isLastRef) {
            // retrieve wrapped __gc, if any
            lua_pushvalue(L_, lua_upvalueindex(1));                                                // L_: self __gc?
            if (!lua_isnil(L_, -1)) {
                lua_insert(L_, -2);                                                                // L_: __gc self
                lua_call(L_, 1, 0);                                                                // L_:
            } else {
                // need an empty stack in case we are GC_ing from a Keeper, so that empty stack checks aren't triggered
                lua_pop(L_, 2);                                                                    // L_:
            }
            DeepFactory::DeleteDeepObject(L_, _p);
        }
        return 0;
    }

    // #############################################################################################

    // Pops the key (metatable or factory) off the stack, and replaces with the deep lookup value (factory/metatable/nil).
    static void LookupDeep(lua_State* const L_)
    {
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 1);                                                              // L_: a
        kDeepLookupRegKey.pushValue(L_);                                                           // L_: a {}
        if (!lua_isnil(L_, -1)) {
            lua_insert(L_, -2);                                                                    // L_: {} a
            lua_rawget(L_, -2);                                                                    // L_: {} b
        }
        lua_remove(L_, -2);                                                                        // L_: a|b
        STACK_CHECK(L_, 1);
    }

    // #############################################################################################
    // #############################################################################################
} // namespace
// #################################################################################################
// #################################################################################################

// #################################################################################################
// #################################################################################################
// ####################################### DeepFactory #############################################
// #################################################################################################
// #################################################################################################

// NEVER call deleteDeepObjectInternal by itself, ALWAYS go through DeleteDeepObject()
void DeepFactory::DeleteDeepObject(lua_State* const L_, DeepPrelude* const o_)
{
    STACK_CHECK_START_REL(L_, 0);
    o_->factory.deleteDeepObjectInternal(L_, o_);
    STACK_CHECK(L_, 0);
}

// #################################################################################################

bool DeepFactory::IsDeepUserdata(lua_State* const L_, int const idx_)
{
    return LookupFactory(L_, idx_, LookupMode::LaneBody) != nullptr;
}

// #################################################################################################

// Return the registered factory for 'index' (deep userdata proxy),  or nullptr if 'index' is not a deep userdata proxy.
DeepFactory* DeepFactory::LookupFactory(lua_State* const L_, int const index_, LookupMode const mode_)
{
    // when looking inside a keeper, we are 100% sure the object is a deep userdata
    if (mode_ == LookupMode::FromKeeper) {
        DeepPrelude* const _proxy{ *luaG_tofulluserdata<DeepPrelude*>(L_, index_) };
        // we can (and must) cast and fetch the internally stored factory
        return &_proxy->factory;
    } else {
        // essentially we are making sure that the metatable of the object we want to copy is stored in our metatable/factory database
        // it is the only way to ensure that the userdata is indeed a deep userdata!
        // of course, we could just trust the caller, but we won't
        STACK_GROW(L_, 1);
        STACK_CHECK_START_REL(L_, 0);

        if (!lua_getmetatable(L_, index_)) {                                                       // L_: deep ... metatable?
            return nullptr; // no metatable: can't be a deep userdata object!
        }

        // replace metatable with the factory pointer, if it is actually a deep userdata
        LookupDeep(L_);                                                                            // L_: deep ... factory|nil

        DeepFactory* const _ret{ luaG_tolightuserdata<DeepFactory>(L_, -1) }; // nullptr if not a userdata
        lua_pop(L_, 1);
        STACK_CHECK(L_, 0);
        return _ret;
    }
}

// #################################################################################################

/*
 * Push a proxy userdata on the stack.
 * raises an error in case of problem (error cannot happen with mode_ == LookupMode::ToKeeper)
 *
 * Initializes necessary structures if it's the first time 'factory' is being used in
 * this Lua state (metatable, registring it). Otherwise, increments the reference count.
 */
void DeepFactory::PushDeepProxy(DestState const L_, DeepPrelude* const prelude_, int const nuv_, LookupMode const mode_, lua_State* const errL_)
{
    STACK_CHECK_START_REL(L_, 0);
    kDeepProxyCacheRegKey.getSubTableMode(L_, "v");                                                // L_: DPC

    // Check if a proxy already exists
    lua_pushlightuserdata(L_, prelude_);                                                           // L_: DPC deep
    lua_rawget(L_, -2);                                                                            // L_: DPC proxy
    if (!lua_isnil(L_, -1)) {
        lua_remove(L_, -2);                                                                        // L_: proxy
        STACK_CHECK(L_, 1);
        return;
    } else {
        lua_pop(L_, 1);                                                                            // L_: DPC
    }
    STACK_CHECK(L_, 1);

    STACK_GROW(L_, 7);

    // a new full userdata, fitted with the specified number of uservalue slots (always 1 for Lua < 5.4)
    DeepPrelude** const _proxy{ luaG_newuserdatauv<DeepPrelude*>(L_, nuv_) };                      // L_: DPC proxy
    LUA_ASSERT(L_, _proxy);
    *_proxy = prelude_;
    prelude_->refcount.fetch_add(1, std::memory_order_relaxed); // one more proxy pointing to this deep data

    // Get/create metatable for 'factory' (in this state)
    DeepFactory& _factory = prelude_->factory;
    lua_pushlightuserdata(L_, std::bit_cast<void*>(&_factory));                                    // L_: DPC proxy _factory
    LookupDeep(L_);                                                                                // L_: DPC proxy metatable|nil

    if (lua_isnil(L_, -1)) { // No metatable yet.
        lua_pop(L_, 1);                                                                            // L_: DPC proxy
        int const _oldtop{ lua_gettop(L_) };
        // 1 - make one and register it
        if (mode_ != LookupMode::ToKeeper) {
            _factory.createMetatable(L_);                                                          // L_: DPC proxy metatable
            if (lua_gettop(L_) - _oldtop != 1 || !lua_istable(L_, -1)) {
                // factory didn't push exactly 1 value, or the value it pushed is not a table: ERROR!
                raise_luaL_error(errL_, "Bad DeepFactory::createMetatable overload: unexpected pushed value");
            }
            // if the metatable contains a __gc, we will call it from our own
            std::ignore = luaG_getfield(L_, -1, "__gc");                                           // L_: DPC proxy metatable __gc
        } else {
            // keepers need a minimal metatable that only contains our own __gc
            lua_createtable(L_, 0, 1);                                                             // L_: DPC proxy metatable
            lua_pushnil(L_);                                                                       // L_: DPC proxy metatable nil
        }
        if (lua_isnil(L_, -1)) {
            // Add our own '__gc' method
            lua_pop(L_, 1);                                                                        // L_: DPC proxy metatable
            lua_pushcfunction(L_, DeepGC);                                                         // L_: DPC proxy metatable DeepGC
        } else {
            // Add our own '__gc' method wrapping the original
            lua_pushcclosure(L_, DeepGC, 1);                                                       // L_: DPC proxy metatable DeepGC
        }
        lua_setfield(L_, -2, "__gc");                                                              // L_: DPC proxy metatable

        // Memorize for later rounds
        _factory.storeDeepLookup(L_);

        // 2 - cause the target state to require the module that exported the factory
        if (std::string_view const _modname{ _factory.moduleName() }; !_modname.empty()) { // we actually got a module name
            // L.registry._LOADED exists without having registered the 'package' library.
            lua_getglobal(L_, "require");                                                          // L_: DPC proxy metatable require()
            // check that the module is already loaded (or being loaded, we are happy either way)
            if (!lua_isfunction(L_, -1)) { // a module name, but no require() function :-(
                raise_luaL_error(errL_, "lanes receiving deep userdata should register the 'package' library");
            }

            luaG_pushstring(L_, _modname);                                                         // L_: DPC proxy metatable require() "module"
            if (luaG_getfield(L_, LUA_REGISTRYINDEX, LUA_LOADED_TABLE) != LuaType::TABLE) {        // L_: DPC proxy metatable require() "module" _R._LOADED
                // no L.registry._LOADED; can this ever happen?
                lua_pop(L_, 6);                                                                    // L_:
                raise_luaL_error(errL_, "unexpected error while requiring a module identified by DeepFactory::moduleName");
            }

            lua_pushvalue(L_, -2);                                                                 // L_: DPC proxy metatable require() "module" _R._LOADED "module"
            lua_rawget(L_, -2);                                                                    // L_: DPC proxy metatable require() "module" _R._LOADED module
            int const _alreadyloaded{ lua_toboolean(L_, -1) };
            if (!_alreadyloaded) { // not loaded
                lua_pop(L_, 2);                                                                    // L_: DPC proxy metatable require() "module"
                // require "modname". in case of error, raise it in errL_
                if (L_ == errL_) {
                    lua_call(L_, 1, 0);                                                            // L_: DPC proxy metatable module
                } else {
                    LuaError const _require_result{ lua_pcall(L_, 1, 0, 0) };                      // L_: DPC proxy metatable error?
                    if (_require_result != LuaError::OK) {
                        // failed, raise the error in the proper state
                        raise_luaL_error(errL_, luaG_tostring(L_, -1));
                    }
                }
            } else { // already loaded, we are happy
                lua_pop(L_, 4);                                                                    // L_: DPC proxy metatable
            }
        }
    }
    STACK_CHECK(L_, 3);                                                                            // L_: DPC proxy metatable
    LUA_ASSERT(L_, luaG_type(L_, -2) == LuaType::USERDATA);
    LUA_ASSERT(L_, lua_istable(L_, -1));
    lua_setmetatable(L_, -2);                                                                      // L_: DPC proxy

    // If we're here, we obviously had to create a new proxy, so cache it.
    lua_pushlightuserdata(L_, prelude_);                                                           // L_: DPC proxy deep
    lua_pushvalue(L_, -2);                                                                         // L_: DPC proxy deep proxy
    lua_rawset(L_, -4);                                                                            // L_: DPC proxy
    lua_remove(L_, -2);                                                                            // L_: proxy
    LUA_ASSERT(L_, luaG_type(L_, -1) == LuaType::USERDATA);
    STACK_CHECK(L_, 1);
}

// #################################################################################################

/*
 * Create a deep userdata
 *
 *   proxy_ud= deep_userdata( [...] )
 *
 * Creates a deep userdata entry of the type defined by the factory.
 * Parameters found on the stack are left as is and passed on to DeepFactory::newDeepObjectInternal.
 *
 * Reference counting and true userdata proxying are taken care of for the actual data type.
 *
 * Types using the deep userdata system (and only those!) can be passed between
 * separate Lua states via 'luaG_inter_move()'.
 *
 * Returns: 'proxy' userdata for accessing the deep data via 'DeepFactory::toDeep()'
 */
void DeepFactory::pushDeepUserdata(DestState const L_, int const nuv_) const
{
    STACK_GROW(L_, 1);
    STACK_CHECK_START_REL(L_, 0);
    int const _oldtop{ lua_gettop(L_) };
    DeepPrelude* const _prelude{ newDeepObjectInternal(L_) };
    if (_prelude == nullptr) {
        raise_luaL_error(L_, "DeepFactory::newDeepObjectInternal failed to create deep userdata (out of memory)");
    }

    if (_prelude->magic != kDeepVersion) {
        // just in case, don't leak the newly allocated deep userdata object
        DeleteDeepObject(L_, _prelude);
        raise_luaL_error(L_, "Bad Deep Factory: kDeepVersion is incorrect, rebuild your implementation with the latest deep implementation");
    }

    LUA_ASSERT(L_, _prelude->refcount.load(std::memory_order_relaxed) == 0); // 'DeepFactory::PushDeepProxy' will lift it to 1
    LUA_ASSERT(L_, &_prelude->factory == this);

    if (lua_gettop(L_) - _oldtop != 0) {
        // just in case, don't leak the newly allocated deep userdata object
        DeleteDeepObject(L_, _prelude);
        raise_luaL_error(L_, "Bad DeepFactory::newDeepObjectInternal overload: should not push anything on the stack");
    }

    DeepFactory::PushDeepProxy(L_, _prelude, nuv_, LookupMode::LaneBody, L_);                      // proxy
    STACK_CHECK(L_, 1);
}

// #################################################################################################

/*
 * Sets up [-1]<->[-2] two-way lookups, and ensures the lookup table exists.
 * Pops the both values off the stack.
 */
void DeepFactory::storeDeepLookup(lua_State* const L_) const
{
    // the deep metatable is at the top of the stack                                               // L_: mt
    STACK_GROW(L_, 3);
    STACK_CHECK_START_REL(L_, 0);                                                                  // L_: mt
    std::ignore = kDeepLookupRegKey.getSubTable(L_, 0, 0);                                         // L_: mt {}
    lua_pushvalue(L_, -2);                                                                         // L_: mt {} mt
    lua_pushlightuserdata(L_, std::bit_cast<void*>(this));                                         // L_: mt {} mt factory
    lua_rawset(L_, -3);                                                                            // L_: mt {}
    STACK_CHECK(L_, 1);

    lua_pushlightuserdata(L_, std::bit_cast<void*>(this));                                         // L_: mt {} factory
    lua_pushvalue(L_, -3);                                                                         // L_: mt {} factory mt
    lua_rawset(L_, -3);                                                                            // L_: mt {}
    STACK_CHECK(L_, 1);

    lua_pop(L_, 1);                                                                                // L_: mt
    STACK_CHECK(L_, 0);
}

// #################################################################################################

/*
 * Access deep userdata through a proxy.
 *
 * Reference count is not changed, and access to the deep userdata is not
 * serialized. It is the module's responsibility to prevent conflicting usage.
 */
DeepPrelude* DeepFactory::toDeep(lua_State* const L_, int const index_) const
{
    STACK_CHECK_START_REL(L_, 0);
    // ensure it is actually a deep userdata we created
    if (LookupFactory(L_, index_, LookupMode::LaneBody) != this) {
        return nullptr; // no metatable, or wrong kind
    }
    STACK_CHECK(L_, 0);

    DeepPrelude** const _proxy{ luaG_tofulluserdata<DeepPrelude*>(L_, index_) };
    return *_proxy;
}

// #################################################################################################

void DeepPrelude::push(lua_State* L_) const
{
    STACK_CHECK_START_REL(L_, 0);
    kDeepProxyCacheRegKey.getSubTableMode(L_, "v");                                                // L_: DPC
    lua_pushlightuserdata(L_, const_cast<DeepPrelude*>(this));                                     // L_: DPC this
    lua_rawget(L_, -2);                                                                            // L_: DPC deep
    lua_remove(L_, -2);                                                                            // L_: deep
    STACK_CHECK(L_, 1);
}
