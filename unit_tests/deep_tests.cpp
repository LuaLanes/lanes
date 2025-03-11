#include "_pch.hpp"
#include "shared.h"

// yeah it's dirty, I will do better someday
#include "../deep_userdata_example/deep_userdata_example.cpp"


// #################################################################################################
// #################################################################################################

TEST_CASE("misc.deep_userdata.example")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess(
        " lanes = require 'lanes'.configure()"
        " fixture = require 'fixture'"
        " due = require 'deep_userdata_example'"
    );

    SECTION("garbage collection collects")
    {
        S.requireSuccess("assert(true)");
        S.requireFailure("assert(false)");
        if constexpr (LUA_VERSION_NUM >= 503) { // Lua < 5.3 only supports a table uservalue
            S.requireSuccess(
                // create a deep userdata object without referencing it. First uservalue is a function, and should be called on __gc
                " due.new_deep(1):setuv(1, function() collected = collected and collected + 1 or 1 end)"
                " due.new_deep(1):setuv(1, function() collected = collected and collected + 1 or 1 end)"
                " collectgarbage()"                         // and collect it
                " assert(collected == 2)"
            );
        }
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("reference counting")
    {
        S.requireSuccess(
            " d = due.new_deep(1)"                         // create a deep userdata object
            " d:set(42)"                                   // set some value
            " assert(d:refcount() == 1)"
        );
        S.requireSuccess(
            " l = lanes.linda()"
            " b, s = l:set('k', d, d)"                     // store it twice in the linda
            " assert(b == false and s == 'under')"         // no waking, under capacity
            " assert(d:refcount() == 2)"                   // 1 ref here, 1 in the keeper (even if we sent it twice)
        );
        S.requireSuccess(
            " n, d = l:get('k')"                           // pull it out of the linda
            " assert(n == 1 and type(d) == 'userdata')"    // got 1 item out of the linda
            " assert(d:get() == 42 and d:refcount() == 2)" // 1 ref here, 1 in the keeper (even if we sent it twice)
        );
        S.requireSuccess(
            " l = nil"
            " collectgarbage()"                            // clears the linda, removes its storage from the keeper
            " lanes.collectgarbage()"                      // collect garbage inside the keepers too, to finish cleanup
            " assert(d:refcount() == 1)"                   // 1 ref here
        );
        if constexpr (LUA_VERSION_NUM >= 503) { // Lua < 5.3 only supports a table uservalue
            S.requireSuccess(
                " d:setuv(1, function() collected = collected and collected + 1 or 1 end)"
                " d = nil"                                 // clear last reference
                " collectgarbage()"                        // force collection
                " assert(collected == 1)"                  // we should see it
            );
        }
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("collection from inside a Linda")
    {
        S.requireSuccess(
            " d = due.new_deep(1)"                         // create a deep userdata object
            " d:set(42)"                                   // set some value
            " assert(d:refcount() == 1)"
        );
        S.requireSuccess(
            " l = lanes.linda()"
            " b, s = l:set('k', d, d)"                     // store it twice in the linda
            " assert(b == false and s == 'under')"         // no waking, under capacity
            " assert(d:refcount() == 2)"                   // 1 ref here, 1 in the keeper (even if we sent it twice)
        );
        S.requireSuccess(
            " d = nil"
            " collectgarbage()"                            // force collection
            " l = nil"
            " collectgarbage()"                            // clears the linda, removes its storage from the keeper
            " lanes.collectgarbage()"                      // collect garbage inside the keepers too, to finish cleanup
            " assert(due.get_deep_count() == 0)"
        );
    }
}