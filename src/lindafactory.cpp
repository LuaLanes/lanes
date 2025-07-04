/*
 * LINDAFACTORY.CPP                    Copyright (c) 2024-, Benoit Germain
 *
 * Linda deep userdata factory
 */

/*
===============================================================================

Copyright (C) 2024- benoit Germain <bnt.germain@gmail.com>

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
#include "lindafactory.hpp"

#include "linda.hpp"

static constexpr std::string_view kLindaMetatableName{ "Linda" };

// #################################################################################################

void LindaFactory::createMetatable(lua_State* L_) const
{
    static constexpr std::string_view kIndex{ "__index" };

    STACK_CHECK_START_REL(L_, 0);
    lua_newtable(L_);                                                                              // L_: mt

    // protect metatable from external access
    luaW_pushstring(L_, kLindaMetatableName);                                                      // L_: mt "<name>"
    lua_setfield(L_, -2, "__metatable");                                                           // L_: mt

    // the linda functions
    luaW_registerlibfuncs(L_, mLindaMT);

    kNilSentinel.pushKey(L_);                                                                      // L_: mt kNilSentinel
    lua_setfield(L_, -2, "null");                                                                  // L_: mt

    // if the metatable contains __index, leave it as is
    if (luaW_getfield(L_, kIdxTop, kIndex) != LuaType::NIL) {                                      // L_: mt __index
        lua_pop(L_, 1);                                                                            // L_: mt __index
    } else {
        // metatable is its own index
        lua_pushvalue(L_, kIdxTop);                                                                // L_: mt mt
        luaW_setfield(L_, StackIndex{ -2 }, kIndex);                                               // L_: mt
    }

    STACK_CHECK(L_, 1);
}

// #################################################################################################

void LindaFactory::deleteDeepObjectInternal(lua_State* L_, DeepPrelude* o_) const
{
    Linda* const _linda{ static_cast<Linda*>(o_) };
    LUA_ASSERT(L_, _linda && !_linda->inKeeperOperation());
    Keeper* const _myKeeper{ _linda->whichKeeper() };
    // if collected after the universe, keepers are already destroyed, and there is nothing to clear
    if (_myKeeper) {
        // if collected from my own keeper, we can't acquire/release it
        // because we are already inside a protected area, and trying to do so would deadlock!
        bool const _need_acquire_release{ _myKeeper->K != L_ };
        // Clean associated structures in the keeper state.
        Keeper* const _keeper{ _need_acquire_release ? _linda->acquireKeeper() : _myKeeper };
        LUA_ASSERT(L_, _keeper == _myKeeper); // should always be the same
        // hopefully this won't ever raise an error as we would jump to the closest pcall site while forgetting to release the keeper mutex...
        [[maybe_unused]] KeeperCallResult const result{ keeper_call(_keeper->K, KEEPER_API(destruct), L_, _linda, kIdxNone) };
        LUA_ASSERT(L_, result.has_value() && result.value() == 0);
        if (_need_acquire_release) {
            _linda->releaseKeeper(_keeper);
        }
    }

    delete _linda; // operator delete overload ensures things go as expected
}

// #################################################################################################

std::string_view LindaFactory::moduleName() const
{
    // linda is a special case because we know lanes must be loaded from the main lua state
    // to be able to ever get here, so we know it will remain loaded as long a the main state is around
    // in other words, forever.
    return std::string_view{};
}

// #################################################################################################

DeepPrelude* LindaFactory::newDeepObjectInternal(lua_State* const L_) const
{
    STACK_CHECK_START_REL(L_, 0);
    // we always expect name, wake_period, group at the bottom of the stack (either can be nil). any extra stuff we ignore and keep unmodified
    std::string_view _linda_name{ luaW_tostring(L_, StackIndex{ 1 }) };
    auto const _wake_period{ static_cast<lua_Duration>(lua_tonumber(L_, 2)) };
    LindaGroup const _linda_group{ static_cast<int>(lua_tointeger(L_, 3)) };

    // store in the linda the location of the script that created it
    if (_linda_name == "auto") {
        lua_Debug _ar;
        if (lua_getstack(L_, 1, &_ar) == 1) { // 1 because we want the name of the function that called lanes.linda (where we currently are)
            lua_getinfo(L_, "Sln", &_ar);
            _linda_name = luaW_pushstring(L_, "%s:%d", _ar.short_src, _ar.currentline);
        } else {
            _linda_name = luaW_pushstring(L_, "<unresolved>");
        }
        // since the name is not empty, it is at slot 1, and we can replace "auto" with the result, just in case
        LUA_ASSERT(L_, luaW_tostring(L_, StackIndex{ 1 }) == "auto");
        lua_replace(L_, 1);
    }

    // The deep data is allocated separately of Lua stack; we might no longer be around when last reference to it is being released.
    // One can use any memory allocation scheme. Just don't use L's allocF because we don't know which state will get the honor of GCing the linda
    Universe* const _U{ Universe::Get(L_) };
    Linda* const _linda{ new (_U) Linda{ _U, _linda_name, _wake_period, _linda_group } };
    STACK_CHECK(L_, 0);
    return _linda;
}
