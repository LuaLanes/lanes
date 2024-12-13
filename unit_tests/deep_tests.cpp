#include "_pch.hpp"
#include "shared.h"

// yeah it's dirty, I will do better someday
#include "../deep_test/deep_test.cpp"


// #################################################################################################
// #################################################################################################

class DeepTests : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

    void SetUp() override
    {
        std::ignore = S.doString("lanes = require 'lanes'.configure()");
        std::ignore = S.doString("fixture = require 'fixture'");
        std::ignore = S.doString("deep_test = require 'deep_test'");
    }
};

// #################################################################################################
// #################################################################################################

TEST_F(DeepTests, DeepIsCollected)
{
    ASSERT_EQ(S.doString("assert(true)"), LuaError::OK) << S;
    ASSERT_NE(S.doString("assert(false)"), LuaError::OK) << S;
    if constexpr (LUA_VERSION_NUM >= 503) { // Lua < 5.3 only supports a table uservalue
        ASSERT_EQ(S.doString(
            // create a deep userdata object without referencing it. First uservalue is a function, and should be called on __gc
            " deep_test.new_deep(1):setuv(1, function() collected = collected and collected + 1 or 1 end)"
            " deep_test.new_deep(1):setuv(1, function() collected = collected and collected + 1 or 1 end)"
            " collectgarbage()"                         // and collect it
            " assert(collected == 2)"
        ), LuaError::OK) << S;
    }
}

// #################################################################################################

TEST_F(DeepTests, DeepRefcounting)
{
    ASSERT_EQ(S.doString(
        " d = deep_test.new_deep(1)"                   // create a deep userdata object
        " d:set(42)"                                   // set some value
        " assert(d:refcount() == 1)"
    ), LuaError::OK) << S;
    ASSERT_EQ(S.doString(
        " l = lanes.linda()"
        " b, s = l:set('k', d, d)"                     // store it twice in the linda
        " assert(b == false and s == 'under')"         // no waking, under capacity
        " assert(d:refcount() == 2)"                   // 1 ref here, 1 in the keeper (even if we sent it twice)
    ), LuaError::OK) << S;
    ASSERT_EQ(S.doString(
        " n, d = l:get('k')"                           // pull it out of the linda
        " assert(n == 1 and type(d) == 'userdata')"    // got 1 item out of the linda
        " assert(d:get() == 42 and d:refcount() == 2)" // 1 ref here, 1 in the keeper (even if we sent it twice)
    ), LuaError::OK) << S;
    ASSERT_EQ(S.doString(
        " l = nil"
        " collectgarbage()"                            // clears the linda, removes its storage from the keeper
        " lanes.collectgarbage()"                      // collect garbage inside the keepers too, to finish cleanup
        " assert(d:refcount() == 1)"                   // 1 ref here
    ), LuaError::OK) << S;
    if constexpr (LUA_VERSION_NUM >= 503) { // Lua < 5.3 only supports a table uservalue
        ASSERT_EQ(S.doString(
            " d:setuv(1, function() collected = collected and collected + 1 or 1 end)"
            " d = nil"                                 // clear last reference
            " collectgarbage()"                        // force collection
            " assert(collected == 1)"                  // we should see it
        ), LuaError::OK) << S;
    }
}

// #################################################################################################

TEST_F(DeepTests, DeepCollectedFromInsideLinda)
{
    ASSERT_EQ(S.doString(
        " d = deep_test.new_deep(1)"                   // create a deep userdata object
        " d:set(42)"                                   // set some value
        " assert(d:refcount() == 1)"
    ), LuaError::OK) << S;
    ASSERT_EQ(S.doString(
        " l = lanes.linda()"
        " b, s = l:set('k', d, d)"                     // store it twice in the linda
        " assert(b == false and s == 'under')"         // no waking, under capacity
        " assert(d:refcount() == 2)"                   // 1 ref here, 1 in the keeper (even if we sent it twice)
    ), LuaError::OK) << S;
    ASSERT_EQ(S.doString(
        " d = nil"
        " collectgarbage()"                            // force collection
        " l = nil"
        " collectgarbage()"                            // clears the linda, removes its storage from the keeper
        " lanes.collectgarbage()"                      // collect garbage inside the keepers too, to finish cleanup
        " assert(deep_test.get_deep_count() == 0)"
    ), LuaError::OK) << S;
}