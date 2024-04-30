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

// xxh64 of string "kUniverseFullRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseFullRegKey{ 0x1C2D76870DD9DD9Full };
// xxh64 of string "kUniverseLightRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kUniverseLightRegKey{ 0x48BBE9CEAB0BA04Full };

// #################################################################################################

Universe::Universe()
{
    //---
    // Linux needs SCHED_RR to change thread priorities, and that is only
    // allowed for sudo'ers. SCHED_OTHER (default) has no priorities.
    // SCHED_OTHER threads are always lower priority than SCHED_RR.
    //
    // ^-- those apply to 2.6 kernel.  IF **wishful thinking** these
    //     constraints will change in the future, non-sudo priorities can
    //     be enabled also for Linux.
    //
#ifdef PLATFORM_LINUX
    // If lower priorities (-2..-1) are wanted, we need to lift the main
    // thread to SCHED_RR and 50 (medium) level. Otherwise, we're always below
    // the launched threads (even -2).
    //
#ifdef LINUX_SCHED_RR
    if (m_sudo) {
        struct sched_param sp;
        sp.sched_priority = _PRIO_0;
        PT_CALL(pthread_setschedparam(pthread_self(), SCHED_RR, &sp));
    }
#endif // LINUX_SCHED_RR
#endif // PLATFORM_LINUX
}

// #################################################################################################

// only called from the master state
Universe* universe_create(lua_State* L_)
{
    LUA_ASSERT(L_, universe_get(L_) == nullptr);
    Universe* const U{ lua_newuserdatauv<Universe>(L_, 0) }; // universe
    U->Universe::Universe();
    STACK_CHECK_START_REL(L_, 1);
    kUniverseFullRegKey.setValue(L_, [](lua_State* L_) { lua_pushvalue(L_, -2); });
    kUniverseLightRegKey.setValue(L_, [U](lua_State* L_) { lua_pushlightuserdata(L_, U); });
    STACK_CHECK(L_, 1);
    return U;
}

// #################################################################################################

void universe_store(lua_State* L_, Universe* U_)
{
    LUA_ASSERT(L_, !U_ || universe_get(L_) == nullptr);
    STACK_CHECK_START_REL(L_, 0);
    kUniverseLightRegKey.setValue(L_, [U = U_](lua_State* L_) { U ? lua_pushlightuserdata(L_, U) : lua_pushnil(L_); });
    STACK_CHECK(L_, 0);
}

// #################################################################################################

Universe* universe_get(lua_State* L_)
{
    STACK_CHECK_START_REL(L_, 0);
    Universe* const universe{ kUniverseLightRegKey.readLightUserDataValue<Universe>(L_) };
    STACK_CHECK(L_, 0);
    return universe;
}
