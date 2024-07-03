/*
===============================================================================

Copyright (C) 2024 Benoit Germain <bnt.germain@gmail.com>

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
#include "tracker.h"

#include "lane.h"

// #################################################################################################

/*
 * Add the lane to tracking chain; the ones still running at the end of the
 * whole process will be cancelled.
 */
void LaneTracker::tracking_add(Lane* lane_)
{
    if (!isActive()) {
        return;
    }
    std::lock_guard<std::mutex> _guard{ trackingMutex };
    assert(lane_->tracking_next == nullptr);

    lane_->tracking_next = trackingFirst;
    trackingFirst = lane_;
}

// #################################################################################################

/*
 * A free-running lane has ended; remove it from tracking chain
 */
[[nodiscard]] bool LaneTracker::tracking_remove(Lane* lane_)
{
    if (!isActive()) {
        return false;
    }

    bool _found{ false };
    std::lock_guard<std::mutex> _guard{ trackingMutex };
    // Make sure (within the MUTEX) that we actually are in the chain
    // still (at process exit they will remove us from chain and then
    // cancel/kill).
    //
    if (lane_->tracking_next != nullptr) {
        Lane** _ref = (Lane**) &trackingFirst;

        while (*_ref != TRACKING_END) {
            if (*_ref == lane_) {
                *_ref = lane_->tracking_next;
                lane_->tracking_next = nullptr;
                _found = true;
                break;
            }
            _ref = (Lane**) &((*_ref)->tracking_next);
        }
        assert(_found);
    }
    return _found;
}

// ################################################################################################

[[nodiscard]] int LaneTracker::pushThreadsTable(lua_State* L_) const
{
    int const _top{ lua_gettop(L_) };
    // List _all_ still running threads
    std::lock_guard<std::mutex> _guard{ trackingMutex };
    if (trackingFirst && trackingFirst != TRACKING_END) {
        Lane* _lane{ trackingFirst };
        int _index{ 0 };
        lua_newtable(L_);                                                                          // L_: {}
        while (_lane != TRACKING_END) {
            // insert a { name='<name>', status='<status>' } tuple, so that several lanes with the same name can't clobber each other
            lua_createtable(L_, 0, 2);                                                             // L_: {} {}
            luaG_pushstring(L_, _lane->getDebugName());                                            // L_: {} {} "name"
            lua_setfield(L_, -2, "name");                                                          // L_: {} {}
            _lane->pushStatusString(L_);                                                           // L_: {} {} "<status>"
            lua_setfield(L_, -2, "status");                                                        // L_: {} {}
            lua_rawseti(L_, -2, ++_index);                                                         // L_: {}
            _lane = _lane->tracking_next;
        }
    }
    return lua_gettop(L_) - _top;                                                                  // L_: 0 or 1
}
