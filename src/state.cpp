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

#include "_pch.h"
#include "state.h"

#include "intercopycontext.h"
#include "lane.h"
#include "lanes.h"
#include "tools.h"
#include "universe.h"

// #################################################################################################
// #################################################################################################
namespace {
    // #############################################################################################
    // #############################################################################################

    namespace local {
        static luaL_Reg const sLibs[] = {
            { "base", nullptr }, // ignore "base" is always valid, but opened separately

#if LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
            { LUA_BITLIBNAME, luaopen_bit32 }, // active in Lua 5.2, replaced with an error-throwing loader in Lua 5.3, gone after.
#endif // LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503

#if LUA_VERSION_NUM >= 502
            { LUA_COLIBNAME, luaopen_coroutine }, // Lua 5.2: coroutine is no longer a part of base!
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
#endif // LUA_VERSION_NUM >= 503

#if LUAJIT_FLAVOR() != 0 // building against LuaJIT headers, add some LuaJIT-specific libs
            { LUA_BITLIBNAME, luaopen_bit },
            { LUA_FFILIBNAME, luaopen_ffi },
            { LUA_JITLIBNAME, luaopen_jit },
#endif // LUAJIT_FLAVOR() != 0

            { kLanesCoreLibName, luaopen_lanes_core } // So that we can open it like any base library (possible since we have access to the init function)
        };

    } // namespace local

    // #############################################################################################

    static void Open1Lib(lua_State* const L_, std::string_view const& name_)
    {
        for (luaL_Reg const& _entry : local::sLibs) {
            if (name_ == _entry.name) {
                lua_CFunction const _libfunc{ _entry.func };
                if (!_libfunc) {
                    break;
                }
                std::string_view const _name{ _entry.name };
                DEBUGSPEW_CODE(DebugSpew(Universe::Get(L_)) << "opening '" << _name << "' library" << std::endl);
                STACK_CHECK_START_REL(L_, 0);
                // open the library as if through require(), and create a global as well if necessary (the library table is left on the stack)
                bool const _isLanesCore{ _libfunc == luaopen_lanes_core }; // don't want to create a global for "lanes.core"
                luaL_requiref(L_, _name.data(), _libfunc, !_isLanesCore);                          // L_: {lib}
                // lanes.core doesn't declare a global, so scan it here and now
                if (_isLanesCore) {
                    tools::PopulateFuncLookupTable(L_, -1, _name);
                }
                lua_pop(L_, 1);                                                                    // L_:
                STACK_CHECK(L_, 0);
                break;
            }
        }
    }

    // #############################################################################################

    // just like lua_xmove, args are (from, to)
    static void CopyOneTimeSettings(Universe* const U_, SourceState const L1_, DestState const L2_)
    {
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U_ });

        STACK_GROW(L1_, 2);
        STACK_CHECK_START_REL(L1_, 0);
        STACK_CHECK_START_REL(L2_, 0);

        DEBUGSPEW_CODE(DebugSpew(U_) << "CopyOneTimeSettings()" << std::endl);

        kConfigRegKey.pushValue(L1_);                                                              // L1_: config
        // copy settings from from source to destination registry
        InterCopyContext _c{ U_, L2_, L1_, {}, {}, {}, {}, {} };
        if (_c.interMove(1) != InterCopyResult::Success) {                                         // L1_:                                           L2_: config
            raise_luaL_error(L1_, "failed to copy settings when loading " kLanesCoreLibName);
        }
        // set L2:_R[kConfigRegKey] = settings
        kConfigRegKey.setValue(L2_, [](lua_State* L_) { lua_insert(L_, -2); });                    // L1_:                                           L2_: config
        STACK_CHECK(L2_, 0);
        STACK_CHECK(L1_, 0);
    }

    // #############################################################################################
    // #############################################################################################
} // namespace
// #################################################################################################
// #################################################################################################

// #################################################################################################
// #################################################################################################
namespace state {
    // #############################################################################################
    // #############################################################################################

    lua_State* CreateState([[maybe_unused]] Universe* const U_, lua_State* const from_, std::string_view const& hint_)
    {
        lua_State* const _L {
            std::invoke(
                [U = U_, from = from_, &hint = hint_]() {
                    if constexpr (LUAJIT_FLAVOR() == 64) {
                        // for some reason, LuaJIT 64 bits does not support creating a state with lua_newstate...
                        return luaL_newstate();
                    } else {
                        lanes::AllocatorDefinition const _def{ U->resolveAllocator(from, hint) };
                        return lua_newstate(_def.allocF, _def.allocUD);
                    }
                }
            )
        };

        if (_L == nullptr) {
            raise_luaL_error(from_, "luaG_newstate() failed while creating state; out of memory");
        }
        return _L;
    }

    // #############################################################################################

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
     */
    lua_State* NewLaneState(Universe* const U_, SourceState const from_, std::optional<std::string_view> const& libs_)
    {
        DestState const _L{ CreateState(U_, from_, "lane") };

        STACK_GROW(_L, 2);
        STACK_CHECK_START_ABS(_L, 0);

        // copy the universe as a light userdata (only the master state holds the full userdata)
        // that way, if Lanes is required in this new state, we'll know we are part of this universe
        Universe::Store(_L, U_);
        STACK_CHECK(_L, 0);

        // we'll need this every time we transfer some C function from/to this state
        kLookupRegKey.setValue(_L, [](lua_State* L_) { lua_newtable(L_); });
        STACK_CHECK(_L, 0);

        // neither libs (not even 'base') nor special init func: we are done
        if (!libs_.has_value() && std::holds_alternative<std::nullptr_t>(U_->onStateCreateFunc)) {
            DEBUGSPEW_CODE(DebugSpew(U_) << "luaG_newstate(nullptr)" << std::endl);
            return _L;
        }

        DEBUGSPEW_CODE(DebugSpew(U_) << "luaG_newstate()" << std::endl);
        DEBUGSPEW_CODE(DebugSpewIndentScope _scope{ U_ });

        // copy settings (for example because it may contain a Lua on_state_create function)
        CopyOneTimeSettings(U_, from_, _L);

        // 'lua.c' stops GC during initialization so perhaps it is a good idea. :)
        lua_gc(_L, LUA_GCSTOP, 0);

        // Anything causes 'base' and 'jit' to be taken in
        std::string_view _libs{};
        if (libs_.has_value()) {
            _libs = libs_.value();
            // special "*" case (mainly to help with LuaJIT compatibility)
            // as we are called from luaopen_lanes_core() already, and that would deadlock
            if (_libs == "*") {
                DEBUGSPEW_CODE(DebugSpew(U_) << "opening ALL standard libraries" << std::endl);
                luaL_openlibs(_L);
                // don't forget lanes.core for regular lane states
                Open1Lib(_L, kLanesCoreLibName);
                _libs = ""; // done with libs
            } else {
                if constexpr (LUAJIT_FLAVOR() != 0) { // building against LuaJIT headers, always open jit
                    DEBUGSPEW_CODE(DebugSpew(U_) << "opening 'jit' library" << std::endl);
                    Open1Lib(_L, LUA_JITLIBNAME);
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
                while (*_p && !std::isalnum(*_p) && *_p != '.') {
                    ++_p;
                }
                // skip name
                _len = 0;
                while (std::isalnum(_p[_len]) || _p[_len] == '.') {
                    ++_len;
                }
                // open library
                Open1Lib(_L, { _p, _len });
            }
        }
        lua_gc(_L, LUA_GCRESTART, 0);

        tools::SerializeRequire(_L);

        // call this after the base libraries are loaded and GC is restarted
        // will raise an error in from_ in case of problem
        U_->callOnStateCreate(_L, from_, LookupMode::LaneBody);

        STACK_CHECK(_L, 0);
        // after all this, register everything we find in our name<->function database
        luaG_pushglobaltable(_L);                                                                  // L: _G
        tools::PopulateFuncLookupTable(_L, -1, {});
        lua_pop(_L, 1);                                                                            // L:
        STACK_CHECK(_L, 0);

        if constexpr (USE_DEBUG_SPEW()) {
            DEBUGSPEW_CODE(DebugSpew(U_) << std::source_location::current().function_name() << " LOOKUP DB CONTENTS" << std::endl);
            DEBUGSPEW_CODE(DebugSpewIndentScope _scope2{ U_ });
            // dump the lookup database contents
            kLookupRegKey.pushValue(_L);                                                           // L: {}
            lua_pushnil(_L);                                                                       // L: {} nil
            while (lua_next(_L, -2)) {                                                             // L: {} k v
                luaG_pushstring(_L, "[");                                                          // L: {} k v "["

                lua_getglobal(_L, "tostring");                                                     // L: {} k v "[" tostring
                lua_pushvalue(_L, -4);                                                             // L: {} k v "[" tostring k
                lua_call(_L, 1, 1);                                                                // L: {} k v "[" 'k'

                luaG_pushstring(_L, "] = ");                                                       // L: {} k v "[" 'k' "] = "

                lua_getglobal(_L, "tostring");                                                     // L: {} k v "[" 'k' "] = " tostring
                lua_pushvalue(_L, -5);                                                             // L: {} k v "[" 'k' "] = " tostring v
                lua_call(_L, 1, 1);                                                                // L: {} k v "[" 'k' "] = " 'v'
                lua_concat(_L, 4);                                                                 // L: {} k v "[k] = v"
                DEBUGSPEW_CODE(DebugSpew(U_) << luaG_tostring(_L, -1) << std::endl);
                lua_pop(_L, 2);                                                                    // L: {} k
            } // lua_next()                                                                        // L: {}
            lua_pop(_L, 1);                                                                        // L:
        }

        STACK_CHECK(_L, 0);
        return _L;
    }

    // #############################################################################################

    // for internal use only: tell lanes.lua which base libraries are actually supported internally
    LUAG_FUNC(supported_libs)
    {
        STACK_CHECK_START_REL(L_, 0);
        lua_newtable(L_);                                                                          // L_: out
        for (luaL_Reg const& _entry : local::sLibs) {
            lua_pushboolean(L_, 1);                                                                // L_: out true
            luaG_setfield(L_, -2, std::string_view{ _entry.name }); // out[name] = true            // L_: out
        }
        STACK_CHECK(L_, 1);
        return 1;
    }

    // #############################################################################################
    // #############################################################################################
} // namespace state
// #################################################################################################
// #################################################################################################
