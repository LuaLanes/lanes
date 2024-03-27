/*
 * UNIVERSE.C                  Copyright (c) 2017, Benoit Germain
 */

/*
===============================================================================

Copyright (C) 2017 Benoit Germain <bnt.germain@gmail.com>

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

#include <string.h>
#include <assert.h>

#include "universe.h"
#include "compat.h"
#include "macros_and_utils.h"
#include "uniquekey.h"

// xxh64 of string "UNIVERSE_FULL_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey UNIVERSE_FULL_REGKEY{ 0x99CA130C09EDC074ull };
// xxh64 of string "UNIVERSE_LIGHT_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey UNIVERSE_LIGHT_REGKEY{ 0x3663C07C742CEB81ull };

// ################################################################################################

// only called from the master state
Universe* universe_create(lua_State* L)
{
    ASSERT_L(universe_get(L) == nullptr);
    Universe* const U = static_cast<Universe*>(lua_newuserdatauv(L, sizeof(Universe), 0));         // universe
    U->Universe::Universe();
    STACK_CHECK_START_REL(L, 1);
    UNIVERSE_FULL_REGKEY.set_registry(L, [](lua_State* L) { lua_pushvalue(L, -2); });
    UNIVERSE_LIGHT_REGKEY.set_registry(L, [U](lua_State* L) { lua_pushlightuserdata( L, U); });
    STACK_CHECK(L, 1);
    return U;
}

// ################################################################################################

void universe_store(lua_State* L, Universe* U)
{
    ASSERT_L(universe_get(L) == nullptr);
    STACK_CHECK_START_REL(L, 0);
    UNIVERSE_LIGHT_REGKEY.set_registry(L, [U](lua_State* L) { U ? lua_pushlightuserdata( L, U) : lua_pushnil( L); });
    STACK_CHECK( L, 0);
}

// ################################################################################################

Universe* universe_get(lua_State* L)
{
    STACK_GROW(L, 2);
    STACK_CHECK_START_REL(L, 0);
    UNIVERSE_LIGHT_REGKEY.query_registry(L);
    Universe* const universe{ lua_tolightuserdata<Universe>(L, -1) }; // nullptr if nil
    lua_pop(L, 1);
    STACK_CHECK(L, 0);
    return universe;
}
