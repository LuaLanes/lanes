/*
* STATE.CPP
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

#include "state.h"

#include "intercopycontext.h"
#include "lane.h"
#include "lanes.h"
#include "tools.h"
#include "universe.h"

#include <source_location>

// #################################################################################################

/*---=== luaG_newstate ===---*/

[[nodiscard]] static int require_lanes_core(lua_State* L_)
{
    // leaves a copy of 'lanes.core' module table on the stack
    luaL_requiref(L_, kLanesCoreLibName, luaopen_lanes_core, 0);
    return 1;
}

// #################################################################################################
namespace {
    namespace local {
        static luaL_Reg const sLibs[] = {
            { "base", nullptr }, // ignore "base" (already acquired it)
#if LUA_VERSION_NUM >= 502
#ifdef luaopen_bit32
            { LUA_BITLIBNAME, luaopen_bit32 },
#endif
            { LUA_COLIBNAME, luaopen_coroutine }, // Lua 5.2: coroutine is no longer a part of base!
#else // LUA_VERSION_NUM
            { LUA_COLIBNAME, nullptr }, // Lua 5.1: part of base package
#endif // LUA_VERSION_NUM
            { LUA_DBLIBNAME, luaopen_debug },
#ifndef PLATFORM_XBOX // no os/io libs on xbox
            { LUA_IOLIBNAME, luaopen_io },
            { LUA_OSLIBNAME, luaopen_os },
#endif // PLATFORM_XBOX
            { LUA_LOADLIBNAME, luaopen_package },
            { LUA_MATHLIBNAME, luaopen_math },
            { LUA_STRLIBNAME, luaopen_string },
            { LUA_TABLIBNAME, luaopen_table },
#if LUA_VERSION_NUM >= 503
            { LUA_UTF8LIBNAME, luaopen_utf8 },
#endif
#if LUAJIT_FLAVOR() != 0 // building against LuaJIT headers, add some LuaJIT-specific libs
            { LUA_BITLIBNAME, luaopen_bit },
            { LUA_FFILIBNAME, luaopen_ffi },
            { LUA_JITLIBNAME, luaopen_jit },
#endif // LUAJIT_FLAVOR()

            { kLanesCoreLibName, require_lanes_core } // So that we can open it like any base library (possible since we have access to the init function)
        };

    } // namespace local
} // namespace

// #################################################################################################

static void open1lib(lua_State* L_, std::string_view const& name_)
{
    for (luaL_Reg const& _entry : local::sLibs) {
        if (name_ == _entry.name) {
            lua_CFunction const _libfunc{ _entry.func };
            if (!_libfunc) {
                break;
            }
            std::string_view const _name{ _entry.name };
            DEBUGSPEW_CODE(DebugSpew(universe_get(L_)) << "opening '" << _name << "' library" << std::endl);
            STACK_CHECK_START_REL(L_, 0);
            // open the library as if through require(), and create a global as well if necessary (the library table is left on the stack)
            bool const isLanesCore{ _libfunc == require_lanes_core }; // don't want to create a global for "lanes.core"
            luaL_requiref(L_, _name.data(), _libfunc, !isLanesCore);                               // L_: {lib}
            // lanes.core doesn't declare a global, so scan it here and now
            if (isLanesCore) {
                tools::PopulateFuncLookupTable(L_, -1, _name);
            }
            lua_pop(L_, 1);                                                                        // L_:
            STACK_CHECK(L_, 0);
            break;
        }
    }
}

// #################################################################################################

// just like lua_xmove, args are (from, to)
static void copy_one_time_settings(Universe* U_, SourceState L1_, DestState L2_)
{
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U_ });

    STACK_GROW(L1_, 2);
    STACK_CHECK_START_REL(L1_, 0);
    STACK_CHECK_START_REL(L2_, 0);

    DEBUGSPEW_CODE(DebugSpew(U_) << "copy_one_time_settings()" << std::endl);

    kConfigRegKey.pushValue(L1_);                                                                  // L1_: config
    // copy settings from from source to destination registry
    InterCopyContext _c{ U_, L2_, L1_, {}, {}, {}, {}, {} };
    if (_c.inter_move(1) != InterCopyResult::Success) {                                            // L1_:                                           L2_: config
        raise_luaL_error(L1_, "failed to copy settings when loading " kLanesCoreLibName);
    }
    // set L2:_R[kConfigRegKey] = settings
    kConfigRegKey.setValue(L2_, [](lua_State* L_) { lua_insert(L_, -2); });                        // L1_:                                           L2_: config
    STACK_CHECK(L2_, 0);
    STACK_CHECK(L1_, 0);
}

// #################################################################################################

static constexpr char const* kOnStateCreate{ "on_state_create" }; // update lanes.lua if the name changes!

void InitializeOnStateCreate(Universe* U_, lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 1);                                                                  // L_: settings
    if (luaG_getfield(L_, -1, kOnStateCreate) != LuaType::NIL) {                                   // L_: settings on_state_create|nil
        // store C function pointer in an internal variable
        U_->onStateCreateFunc = lua_tocfunction(L_, -1);                                           // L_: settings on_state_create
        if (U_->onStateCreateFunc != nullptr) {
            // make sure the function doesn't have upvalues
            char const* _upname{ lua_getupvalue(L_, -1, 1) };                                      // L_: settings on_state_create upval?
            if (_upname != nullptr) { // should be "" for C functions with upvalues if any
                raise_luaL_error(L_, "%s shouldn't have upvalues", kOnStateCreate);
            }
            // remove this C function from the config table so that it doesn't cause problems
            // when we transfer the config table in newly created Lua states
            lua_pushnil(L_);                                                                       // L_: settings on_state_create nil
            lua_setfield(L_, -3, kOnStateCreate);                                                  // L_: settings on_state_create
        } else {
            // optim: store marker saying we have such a function in the config table
            U_->onStateCreateFunc = reinterpret_cast<lua_CFunction>(InitializeOnStateCreate);
        }
    }
    lua_pop(L_, 1);                                                                                // L_: settings
    STACK_CHECK(L_, 1);
}

// #################################################################################################

lua_State* create_state([[maybe_unused]] Universe* U_, lua_State* from_)
{
    lua_State* const _L {
        std::invoke(
            [U = U_, from = from_]() {
                if constexpr (LUAJIT_FLAVOR() == 64) {
                    // for some reason, LuaJIT 64 bits does not support creating a state with lua_newstate...
                    return luaL_newstate();
                } else {
                    if (U->provideAllocator != nullptr) { // we have a function we can call to obtain an allocator
                        lua_pushcclosure(from, U->provideAllocator, 0);
                        lua_call(from, 0, 1);
                        AllocatorDefinition* const _def{ lua_tofulluserdata<AllocatorDefinition>(from, -1) };
                        lua_State* const _L{ lua_newstate(_def->allocF, _def->allocUD) };
                        lua_pop(from, 1);
                        return _L;
                    } else {
                        // reuse the allocator provided when the master state was created
                        return lua_newstate(U->protectedAllocator.allocF, U->protectedAllocator.allocUD);
                    }
                }
            }
        )
    };

    if (_L == nullptr) {
        raise_luaL_error(from_, "luaG_newstate() failed while creating state; out of memory");
    }
    return _L;
}

// #################################################################################################

void CallOnStateCreate(Universe* U_, lua_State* L_, lua_State* from_, LookupMode mode_)
{
    if (U_->onStateCreateFunc == nullptr) {
        return;
    }

    STACK_CHECK_START_REL(L_, 0);
    DEBUGSPEW_CODE(DebugSpew(U_) << "calling on_state_create()" << std::endl);
    if (U_->onStateCreateFunc != reinterpret_cast<lua_CFunction>(InitializeOnStateCreate)) {
        // C function: recreate a closure in the new state, bypassing the lookup scheme
        lua_pushcfunction(L_, U_->onStateCreateFunc); // on_state_create()
    } else { // Lua function located in the config table, copied when we opened "lanes.core"
        if (mode_ != LookupMode::LaneBody) {
            // if attempting to call in a keeper state, do nothing because the function doesn't exist there
            // this doesn't count as an error though
            STACK_CHECK(L_, 0);
            return;
        }
        kConfigRegKey.pushValue(L_);                                                               // L_: {}
        STACK_CHECK(L_, 1);
        std::ignore = luaG_getfield(L_, -1, kOnStateCreate);                                       // L_: {} on_state_create()
        lua_remove(L_, -2);                                                                        // L_: on_state_create()
    }
    STACK_CHECK(L_, 1);
    // capture error and raise it in caller state
    std::string_view const _stateType{ mode_ == LookupMode::LaneBody ? "lane" : "keeper" };
    std::ignore = lua_pushstringview(L_, _stateType);                                              // L_: on_state_create() "<type>"
    if (lua_pcall(L_, 1, 0, 0) != LUA_OK) {
        raise_luaL_error(from_, "%s failed: \"%s\"", kOnStateCreate, lua_isstring(L_, -1) ? lua_tostring(L_, -1) : lua_typename(L_, lua_type(L_, -1)));
    }
    STACK_CHECK(L_, 0);
}

// #################################################################################################

/*
 * Like 'luaL_openlibs()' but allows the set of libraries be selected
 *
 *   nullptr    no libraries, not even base
 *   ""      base library only
 *   "io,string"     named libraries
 *   "*"     all libraries
 *
 * Base ("unpack", "print" etc.) is always added, unless 'libs' is nullptr.
 *
 * *NOT* called for keeper states!
 *
 */
lua_State* luaG_newstate(Universe* U_, SourceState from_, std::optional<std::string_view> const& libs_)
{
    DestState const _L{ create_state(U_, from_) };

    STACK_GROW(_L, 2);
    STACK_CHECK_START_ABS(_L, 0);

    // copy the universe as a light userdata (only the master state holds the full userdata)
    // that way, if Lanes is required in this new state, we'll know we are part of this universe
    universe_store(_L, U_);
    STACK_CHECK(_L, 0);

    // we'll need this every time we transfer some C function from/to this state
    kLookupRegKey.setValue(_L, [](lua_State* L_) { lua_newtable(L_); });
    STACK_CHECK(_L, 0);

    // neither libs (not even 'base') nor special init func: we are done
    if (!libs_.has_value() && U_->onStateCreateFunc == nullptr) {
        DEBUGSPEW_CODE(DebugSpew(U_) << "luaG_newstate(nullptr)" << std::endl);
        return _L;
    }

    DEBUGSPEW_CODE(DebugSpew(U_) << "luaG_newstate()" << std::endl);
    DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U_ });

    // copy settings (for example because it may contain a Lua on_state_create function)
    copy_one_time_settings(U_, from_, _L);

    // 'lua.c' stops GC during initialization so perhaps it is a good idea. :)
    lua_gc(_L, LUA_GCSTOP, 0);

    // Anything causes 'base' and 'jit' to be taken in
    std::string_view _libs{ libs_.value() };
    if (libs_.has_value()) {
        // special "*" case (mainly to help with LuaJIT compatibility)
        // as we are called from luaopen_lanes_core() already, and that would deadlock
        if (_libs == "*") {
            DEBUGSPEW_CODE(DebugSpew(U_) << "opening ALL standard libraries" << std::endl);
            luaL_openlibs(_L);
            // don't forget lanes.core for regular lane states
            open1lib(_L, kLanesCoreLibName);
            _libs = ""; // done with libs
        } else {
            if constexpr (LUAJIT_FLAVOR() != 0) { // building against LuaJIT headers, always open jit
                DEBUGSPEW_CODE(DebugSpew(U_) << "opening 'jit' library" << std::endl);
                open1lib(_L, LUA_JITLIBNAME);
            }
            DEBUGSPEW_CODE(DebugSpew(U_) << "opening 'base' library" << std::endl);
            if constexpr (LUA_VERSION_NUM >= 502) {
                // open base library the same way as in luaL_openlibs()
                luaL_requiref(_L, LUA_GNAME, luaopen_base, 1);
                lua_pop(_L, 1);
            } else {
                lua_pushcfunction(_L, luaopen_base);
                lua_pushstring(_L, "");
                lua_call(_L, 1, 0);
            }
        }
    }
    STACK_CHECK(_L, 0);

    // scan all libraries, open them one by one
    if (!_libs.empty()) {
        unsigned int _len{ 0 };
        for (char const* _p{ _libs.data() }; *_p; _p += _len) {
            // skip delimiters ('.' can be part of name for "lanes.core")
            while (*_p && !isalnum(*_p) && *_p != '.')
                ++_p;
            // skip name
            _len = 0;
            while (isalnum(_p[_len]) || _p[_len] == '.')
                ++_len;
            // open library
            open1lib(_L, { _p, _len });
        }
    }
    lua_gc(_L, LUA_GCRESTART, 0);

    tools::SerializeRequire(_L);

    // call this after the base libraries are loaded and GC is restarted
    // will raise an error in from_ in case of problem
    CallOnStateCreate(U_, _L, from_, LookupMode::LaneBody);

    STACK_CHECK(_L, 0);
    // after all this, register everything we find in our name<->function database
    lua_pushglobaltable(_L);                                                                       // L: _G
    tools::PopulateFuncLookupTable(_L, -1, {});
    lua_pop(_L, 1);                                                                                // L:
    STACK_CHECK(_L, 0);

    if constexpr (USE_DEBUG_SPEW()) {
        DEBUGSPEW_CODE(DebugSpew(U_) << std::source_location::current().function_name() << " LOOKUP DB CONTENTS" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope2{ U_ });
        // dump the lookup database contents
        kLookupRegKey.pushValue(_L);                                                               // L: {}
        lua_pushnil(_L);                                                                           // L: {} nil
        while (lua_next(_L, -2)) {                                                                 // L: {} k v
            std::ignore = lua_pushstringview(_L, "[");                                             // L: {} k v "["

            lua_getglobal(_L, "tostring");                                                         // L: {} k v "[" tostring
            lua_pushvalue(_L, -4);                                                                 // L: {} k v "[" tostring k
            lua_call(_L, 1, 1);                                                                    // L: {} k v "[" 'k'

            std::ignore = lua_pushstringview(_L, "] = ");                                          // L: {} k v "[" 'k' "] = "

            lua_getglobal(_L, "tostring");                                                         // L: {} k v "[" 'k' "] = " tostring
            lua_pushvalue(_L, -5);                                                                 // L: {} k v "[" 'k' "] = " tostring v
            lua_call(_L, 1, 1);                                                                    // L: {} k v "[" 'k' "] = " 'v'
            lua_concat(_L, 4);                                                                     // L: {} k v "[k] = v"
            DEBUGSPEW_CODE(DebugSpew(U_) << lua_tostringview(_L, -1) << std::endl);
            lua_pop(_L, 2);                                                                        // L: {} k
        } // lua_next()                                                                            // L: {}
        lua_pop(_L, 1);                                                                            // L:
    }

    STACK_CHECK(_L, 0);
    return _L;
}
