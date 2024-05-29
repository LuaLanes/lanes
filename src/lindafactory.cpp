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

#include "lindafactory.h"

#include "linda.h"

// must be a #define instead of a constexpr to work with lua_pushliteral (until I templatize it)
#define kLindaMetatableName "Linda"

// #################################################################################################

void LindaFactory::createMetatable(lua_State* L_) const
{
    STACK_CHECK_START_REL(L_, 0);
    lua_newtable(L_);
    // metatable is its own index
    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "__index");

    // protect metatable from external access
    lua_pushliteral(L_, kLindaMetatableName);
    lua_setfield(L_, -2, "__metatable");

    // the linda functions
    luaG_registerlibfuncs(L_, mLindaMT);

    // some constants
    kLindaBatched.pushKey(L_);
    lua_setfield(L_, -2, "batched");

    kNilSentinel.pushKey(L_);
    lua_setfield(L_, -2, "null");

    STACK_CHECK(L_, 1);
}

// #################################################################################################

void LindaFactory::deleteDeepObjectInternal(lua_State* L_, DeepPrelude* o_) const
{
    Linda* const _linda{ static_cast<Linda*>(o_) };
    LUA_ASSERT(L_, _linda);
    Keeper* const _myK{ _linda->whichKeeper() };
    // if collected after the universe, keepers are already destroyed, and there is nothing to clear
    if (_myK) {
        // if collected from my own keeper, we can't acquire/release it
        // because we are already inside a protected area, and trying to do so would deadlock!
        bool const _need_acquire_release{ _myK->L != L_ };
        // Clean associated structures in the keeper state.
        Keeper* const _K{ _need_acquire_release ? _linda->acquireKeeper() : _myK };
        // hopefully this won't ever raise an error as we would jump to the closest pcall site while forgetting to release the keeper mutex...
        [[maybe_unused]] KeeperCallResult const result{ keeper_call(_K->L, KEEPER_API(clear), L_, _linda, 0) };
        LUA_ASSERT(L_, result.has_value() && result.value() == 0);
        if (_need_acquire_release) {
            _linda->releaseKeeper(_K);
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

DeepPrelude* LindaFactory::newDeepObjectInternal(lua_State* L_) const
{
    std::string_view _linda_name{};
    LindaGroup _linda_group{ 0 };
    // should have a string and/or a number of the stack as parameters (name and group)
    switch (lua_gettop(L_)) {
    default: // 0
        break;

    case 1: // 1 parameter, either a name or a group
        if (lua_type(L_, -1) == LUA_TSTRING) {
            _linda_name = lua_tostringview(L_, -1);
        } else {
            _linda_group = LindaGroup{ static_cast<int>(lua_tointeger(L_, -1)) };
        }
        break;

    case 2: // 2 parameters, a name and group, in that order
        _linda_name = lua_tostringview(L_, -2);
        _linda_group = LindaGroup{ static_cast<int>(lua_tointeger(L_, -1)) };
        break;
    }

    // The deep data is allocated separately of Lua stack; we might no longer be around when last reference to it is being released.
    // One can use any memory allocation scheme. Just don't use L's allocF because we don't know which state will get the honor of GCing the linda
    Universe* const _U{ Universe::Get(L_) };
    Linda* const _linda{ new (_U) Linda{ _U, _linda_group, _linda_name } };
    return _linda;
}
