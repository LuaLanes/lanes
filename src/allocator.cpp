/*
 * ALLOCATOR.CPP                  Copyright (c) 2017-2024, Benoit Germain
 */

/*
===============================================================================

Copyright (C) 2017-2024 Benoit Germain <bnt.germain@gmail.com>

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
#include "allocator.hpp"

namespace lanes
{
    AllocatorDefinition& AllocatorDefinition::Validated(lua_State* const L_, StackIndex const idx_)
    {
        lanes::AllocatorDefinition* const _def{ luaW_tofulluserdata<lanes::AllocatorDefinition>(L_, idx_) };
        // raise an error and don't return if the full userdata at the specified index is not a valid AllocatorDefinition
        if (!_def) {
            raise_luaL_error(L_, "Bad config.allocator function, provided value is not a userdata");
        }
        if (lua_rawlen(L_, idx_) < sizeof(lanes::AllocatorDefinition)) {
            raise_luaL_error(L_, "Bad config.allocator function, provided value is too small to contain a valid AllocatorDefinition");
        }
        if (_def->version != kAllocatorVersion) {
            raise_luaL_error(L_, "Bad config.allocator function, AllocatorDefinition version mismatch");
        }
        return *_def;
    }
}
