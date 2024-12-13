#include "_pch.hpp"
#include "shared.h"

// #################################################################################################
// #################################################################################################

class LaneTests : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    void SetUp() override
    {
        std::ignore = S.doString("lanes = require 'lanes'.configure()");
    }
};

// #################################################################################################

TEST_F(LaneTests, GeneratorCreation)
{
    // no parameter is bad
    EXPECT_NE(S.doString("lanes.gen()"), LuaError::OK);

    // minimal generator needs a function
    EXPECT_EQ(S.doString("lanes.gen(function() end)"), LuaError::OK) << S;

    // acceptable parameters for the generator are strings, tables, nil, followed by the function body
    EXPECT_EQ(S.doString("lanes.gen(nil, function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen({}, function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('', {}, function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen({}, '', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('', '', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen({}, {}, function() end)"), LuaError::OK) << S;

    // anything different should fail: booleans, numbers, any userdata
    EXPECT_NE(S.doString("lanes.gen(false, function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen(true, function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen(42, function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen(io.stdin, function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen(lanes.linda(), function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen(lanes.linda():deep(), function() end)"), LuaError::OK);

    // even if parameter types are correct, the function must come last
    EXPECT_NE(S.doString("lanes.gen(function() end, '')"), LuaError::OK);

    // the strings should only list "known base libraries", in any order, or "*"
    // if the particular Lua flavor we build for doesn't support them, they raise an error unless postfixed by '?'
    EXPECT_EQ(S.doString("lanes.gen('base', function() end)"), LuaError::OK) << S;

    // bit, ffi, jit are LuaJIT-specific
#if LUAJIT_FLAVOR() == 0
    EXPECT_NE(S.doString("lanes.gen('bit,ffi,jit', function() end)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.gen('bit?,ffi?,jit?', function() end)"), LuaError::OK) << S;
#endif // LUAJIT_FLAVOR()

    // bit32 library existed only in Lua 5.2, there is still a loader that will raise an error in Lua 5.3
#if LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
    EXPECT_EQ(S.doString("lanes.gen('bit32', function() end)"), LuaError::OK) << S;
#else // LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
    EXPECT_NE(S.doString("lanes.gen('bit32', function() end)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.gen('bit32?', function() end)"), LuaError::OK) << S;
#endif // LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503

    // coroutine library appeared with Lua 5.2
#if LUA_VERSION_NUM == 501
    EXPECT_NE(S.doString("lanes.gen('coroutine', function() end)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.gen('coroutine?', function() end)"), LuaError::OK) << S;
#endif // LUA_VERSION_NUM == 501

    EXPECT_EQ(S.doString("lanes.gen('debug', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('io', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('math', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('os', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('package', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('string', function() end)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.gen('table', function() end)"), LuaError::OK) << S;

    // utf8 library appeared with Lua 5.3
#if LUA_VERSION_NUM < 503
    EXPECT_NE(S.doString("lanes.gen('utf8', function() end)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.gen('utf8?', function() end)"), LuaError::OK) << S;
#endif // LUA_VERSION_NUM < 503

    EXPECT_EQ(S.doString("lanes.gen('lanes.core', function() end)"), LuaError::OK) << S;
    // "*" repeated or combined with anything else is forbidden
    EXPECT_NE(S.doString("lanes.gen('*', '*', function() end)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.gen('base', '*', function() end)"), LuaError::OK);
    // unknown names are forbidden
    EXPECT_NE(S.doString("lanes.gen('Base', function() end)"), LuaError::OK);
    // repeating the same library more than once is forbidden
    EXPECT_NE(S.doString("lanes.gen('base,base', function() end)"), LuaError::OK);
}

// #################################################################################################

TEST_F(LaneTests, UncooperativeShutdown)
{
    // prepare a callback for lanes.finally()
    static bool _wasCalled{};
    static bool _allLanesTerminated{};
    auto _finallyCB{ +[](lua_State* const L_) { _wasCalled = true; _allLanesTerminated = lua_toboolean(L_, 1); return 0; } };
    lua_pushcfunction(S, _finallyCB);
    lua_setglobal(S, "finallyCB");
    // start a lane that lasts a long time
    std::string_view const script{
        " lanes.finally(finallyCB)"
        " print ('in Master')"
        " f = lanes.gen('*',"
        "     {name = 'auto'},"
        "     function()"
        "         for i = 1,1e37 do end" // no cooperative cancellation checks here!
        "     end)"
        " f()"
    };
    ASSERT_EQ(S.doString(script), LuaError::OK) << S;
    // close the state before the lane ends.
    // since we don't wait at all, it is possible that the OS thread for the lane hasn't even started at that point
    S.close();
    // the finally handler should have been called, and told all lanes are stopped
    ASSERT_EQ(_wasCalled, true) << S;
    ASSERT_EQ(_allLanesTerminated, true) << S;
}

// #################################################################################################
// #################################################################################################

class LaneCancel : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

    void SetUp() override
    {
        // need the timers so that there is a lane running on which we can operate
        std::ignore = S.doString("timer_lane = require 'lanes'.configure{with_timers = true}.timer_lane");
    }
};

// #################################################################################################

TEST_F(LaneCancel, FailsOnBadArguments)
{
    //FAIL() << "Make sure the failures fail the way we expect them";
    // make sure we have the timer lane and its cancel method handy
    ASSERT_EQ(S.doString("assert(timer_lane and timer_lane.cancel)"), LuaError::OK);
    // as well as the fixture module
    ASSERT_EQ(S.doString("fixture = require 'fixture'"), LuaError::OK) << S;

    // cancel operation must be a known string
    ASSERT_NE(S.doString("timer_lane:cancel('gleh')"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel(function() end)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel({})"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel(fixture.newuserdata())"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel(fixture.newlightuserdata())"), LuaError::OK);

    // cancel doesn't expect additional non-number/bool arguments after the mode
    ASSERT_NE(S.doString("timer_lane:cancel('soft', 'gleh')"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('soft', function() end)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('soft', {})"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('soft', fixture.newuserdata())"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('soft', fixture.newlightuserdata())"), LuaError::OK);

    // hook-based cancellation expects a number for the count. IOW, a bool is not valid
    ASSERT_NE(S.doString("timer_lane:cancel('call', true)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('ret', true)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('line', true)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('count', true)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('all', true)"), LuaError::OK);

    // non-hook should only have one number after the mode (the timeout), else it means we have a count
    ASSERT_NE(S.doString("timer_lane:cancel('hard', 10, 10)"), LuaError::OK);

    // extra arguments are not accepted either
    ASSERT_NE(S.doString("timer_lane:cancel('hard', 10, true, 10)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('call', 10, 10, 10)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('line', 10, 10, true, 10)"), LuaError::OK);

    // out-of-range hook count is not valid
    ASSERT_NE(S.doString("timer_lane:cancel('call', -1)"), LuaError::OK);
    ASSERT_NE(S.doString("timer_lane:cancel('call', 0)"), LuaError::OK);

    // out-of-range duration is not valid
    ASSERT_NE(S.doString("timer_lane:cancel('soft', -1)"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################

INSTANTIATE_TEST_CASE_P(
    LaneScriptedTests,
    UnitTestRunner,
    ::testing::Values(
        FileRunnerParam{ PUC_LUA_ONLY("lane/cooperative_shutdown"), TestType::AssertNoLuaError },  // 0
        FileRunnerParam{ "lane/uncooperative_shutdown", TestType::AssertThrows },
        FileRunnerParam{ "lane/tasking_basic", TestType::AssertNoLuaError },                       // 2
        FileRunnerParam{ "lane/tasking_cancelling", TestType::AssertNoLuaError },                  // 3
        FileRunnerParam{ "lane/tasking_comms_criss_cross", TestType::AssertNoLuaError },           // 4
        FileRunnerParam{ "lane/tasking_communications", TestType::AssertNoLuaError },
        FileRunnerParam{ "lane/tasking_error", TestType::AssertNoLuaError },                       // 6
        FileRunnerParam{ "lane/tasking_join_test", TestType::AssertNoLuaError },                   // 7
        FileRunnerParam{ "lane/tasking_send_receive_code", TestType::AssertNoLuaError },
        FileRunnerParam{ "lane/stdlib_naming", TestType::AssertNoLuaError },
        FileRunnerParam{ "coro/basics", TestType::AssertNoLuaError },                              // 10
        FileRunnerParam{ "coro/error_handling", TestType::AssertNoLuaError }
    )
    //,[](::testing::TestParamInfo<FileRunnerParam> const& info_) { return std::string{  info_.param.script  };}
);
