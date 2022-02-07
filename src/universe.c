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

// crc64/we of string "UNIVERSE_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( UNIVERSE_REGKEY, 0x9f877b2cf078f17f);

// ################################################################################################

Universe* universe_create( lua_State* L)
{
    Universe* U = (Universe*) lua_newuserdatauv( L, sizeof(Universe), 0);                          // universe
    memset( U, 0, sizeof( Universe));
    STACK_CHECK( L, 1);
    REGISTRY_SET( L, UNIVERSE_REGKEY, lua_pushvalue(L, -2));                                       // universe
    STACK_END( L, 1);
    return U;
}

// ################################################################################################

void universe_store( lua_State* L, Universe* U)
{
    STACK_CHECK( L, 0);
    REGISTRY_SET( L, UNIVERSE_REGKEY, (NULL != U) ? lua_pushlightuserdata( L, U) : lua_pushnil( L));
    STACK_END( L, 0);
}

// ################################################################################################

Universe* universe_get( lua_State* L)
{
    Universe* universe;
    STACK_GROW( L, 2);
    STACK_CHECK( L, 0);
    REGISTRY_GET( L, UNIVERSE_REGKEY);
    universe = lua_touserdata( L, -1); // NULL if nil
    lua_pop( L, 1);
    STACK_END( L, 0);
    return universe;
}
