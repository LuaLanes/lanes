#include "_pch.hpp"
#include "shared.h"

// #################################################################################################
// #################################################################################################

TEST_CASE("lanes.nameof")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    // no argument is not good
    S.requireFailure("local t, n = lanes.nameof()");

    // more than one argument is not good
    S.requireFailure("local t, n = lanes.nameof(true, false)");

    // a constant is itself, stringified
    S.requireReturnedString("local t, n = lanes.nameof('bob'); return t .. ': ' .. tostring(n)", "string: bob");
    S.requireReturnedString("local t, n = lanes.nameof(true); return t .. ': ' .. tostring(n)", "boolean: true");
    S.requireReturnedString("local t, n = lanes.nameof(42); return t .. ': ' .. tostring(n)", "number: 42");

    // a temporary object has no name
    S.requireReturnedString("local t, n = lanes.nameof({}); return t .. ': ' .. tostring(n)", "table: nil");
    S.requireReturnedString("local t, n = lanes.nameof(function() end); return t .. ': ' .. tostring(n)", "function: nil");

    // look for something in _G
    S.requireReturnedString("local t, n = lanes.nameof(print); return t .. ': ' .. tostring(n)", "function: _G/print()");
    S.requireReturnedString("local t, n = lanes.nameof(string); return t .. ': ' .. tostring(n)", "table: _G/string[]");
    S.requireReturnedString("local t, n = lanes.nameof(string.sub); return t .. ': ' .. tostring(n)", "function: _G/string[]/sub()");
}

// #################################################################################################
// #################################################################################################

TEST_CASE("lanes.sleep.argument validation")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    // anything not a number is no good
    S.requireFailure("lanes.sleep(true)");
    S.requireFailure("lanes.sleep({})");
    S.requireFailure("lanes.sleep('a string')");
    S.requireFailure("lanes.sleep(lanes.null)");
    S.requireFailure("lanes.sleep(print)");

    // negative durations are not supported
    S.requireFailure("lanes.sleep(-1)");

    // no duration is supported (same as 0)
    S.requireSuccess("lanes.sleep()");
    S.requireSuccess("lanes.sleep(0)");
}

// #################################################################################################

TEST_CASE("lanes.sleep.check durations")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    // requesting to sleep some duration should result in sleeping for that duration
    auto const _before{ std::chrono::steady_clock::now() };
    S.requireSuccess("lanes.sleep(1)");
    CHECK(std::chrono::steady_clock::now() - _before >= 1000ms);
    CHECK(std::chrono::steady_clock::now() - _before < 1100ms);
}


// #################################################################################################

TEST_CASE("lanes.sleep.interactions with timers")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'.configure{with_timers = true}");
    
    std::string_view const _script{
        // start a 10Hz timer
        " local l = lanes.linda()"
        " lanes.timer(l, 'gluh', 0.1, 0.1)"
        // launch a lane that is supposed to sleep forever
        " local g = lanes.gen('*', lanes.sleep)"
        " local h = g('indefinitely')"
        // sleep 1 second (this uses the timer linda)
        " lanes.sleep(1)"
        // shutdown should be able to cancel the lane and stop it instantly
        " return 'SUCCESS'"
    };
    // running the script should take about 1 second
    auto const _before{ std::chrono::steady_clock::now() };
    S.requireReturnedString(_script, "SUCCESS");
    CHECK(std::chrono::steady_clock::now() - _before >= 1000ms);
    CHECK(std::chrono::steady_clock::now() - _before < 1100ms);
}

// #################################################################################################
// #################################################################################################

TEST_CASE("lanes.gen")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    // ---------------------------------------------------------------------------------------------

    SECTION("argument checks")
    {
        // no parameter is bad
        S.requireFailure("lanes.gen()");

        // minimal generator needs a function
        S.requireSuccess("lanes.gen(function() end)");

        // acceptable parameters for the generator are strings, tables, nil, followed by the function body
        S.requireSuccess("lanes.gen(nil, function() end)");
        S.requireSuccess("lanes.gen('', function() end)");
        S.requireSuccess("lanes.gen({}, function() end)");
        S.requireSuccess("lanes.gen('', {}, function() end)");
        S.requireSuccess("lanes.gen({}, '', function() end)");
        S.requireSuccess("lanes.gen('', '', function() end)");
        S.requireSuccess("lanes.gen({}, {}, function() end)");

        // anything different should fail: booleans, numbers, any userdata
        S.requireFailure("lanes.gen(false, function() end)");
        S.requireFailure("lanes.gen(true, function() end)");
        S.requireFailure("lanes.gen(42, function() end)");
        S.requireFailure("lanes.gen(io.stdin, function() end)");
        S.requireFailure("lanes.gen(lanes.linda(), function() end)");
        S.requireFailure("lanes.gen(lanes.linda():deep(), function() end)");

        // even if parameter types are correct, the function must come last
        S.requireFailure("lanes.gen(function() end, '')");

        // the strings should only list "known base libraries", in any order, or "*"
        // if the particular Lua flavor we build for doesn't support them, they raise an error unless postfixed by '?'
        S.requireSuccess("lanes.gen('base', function() end)");

        // bit, ffi, jit are LuaJIT-specific
#if LUAJIT_FLAVOR() == 0
        S.requireFailure("lanes.gen('bit,ffi,jit', function() end)");
        S.requireSuccess("lanes.gen('bit?,ffi?,jit?', function() end)");
#endif // LUAJIT_FLAVOR()

        // bit32 library existed only in Lua 5.2, there is still a loader that will raise an error in Lua 5.3
#if LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
        S.requireSuccess("lanes.gen('bit32', function() end)");
#else // LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503
        S.requireFailure("lanes.gen('bit32', function() end)");
        S.requireSuccess("lanes.gen('bit32?', function() end)");
#endif // LUA_VERSION_NUM == 502 || LUA_VERSION_NUM == 503

        // coroutine library appeared with Lua 5.2
#if LUA_VERSION_NUM == 501
        S.requireFailure("lanes.gen('coroutine', function() end)");
        S.requireSuccess("lanes.gen('coroutine?', function() end)");
#endif // LUA_VERSION_NUM == 501

        S.requireSuccess("lanes.gen('debug', function() end)");
        S.requireSuccess("lanes.gen('io', function() end)");
        S.requireSuccess("lanes.gen('math', function() end)");
        S.requireSuccess("lanes.gen('os', function() end)");
        S.requireSuccess("lanes.gen('package', function() end)");
        S.requireSuccess("lanes.gen('string', function() end)");
        S.requireSuccess("lanes.gen('table', function() end)");

        // utf8 library appeared with Lua 5.3
#if LUA_VERSION_NUM < 503
        S.requireFailure("lanes.gen('utf8', function() end)");
        S.requireSuccess("lanes.gen('utf8?', function() end)");
#endif // LUA_VERSION_NUM < 503

        S.requireSuccess("lanes.gen('lanes.core', function() end)");
        // "*" repeated or combined with anything else is forbidden
        S.requireFailure("lanes.gen('*', '*', function() end)");
        S.requireFailure("lanes.gen('base', '*', function() end)");
        // unknown names are forbidden
        S.requireFailure("lanes.gen('Base', function() end)");
        // repeating the same library more than once is forbidden
        S.requireFailure("lanes.gen('base,base', function() end)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("default thread name is '<unnamed>'")
    {
        std::string_view const _script{
            " g = lanes.gen('*',"
            "     function()"
            "         return lane_threadname()"
            "     end)"
            " h = g()"
            " local tn = h[1]"
            " assert(tn == h:get_threadname())"
            " assert(tn == '<unnamed>')"
        };
        S.requireSuccess(_script);
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("set thread name from generator settings")
    {
        std::string_view const _script{
            " g = lanes.gen('*',"
            "     { name = 'user name'},"
            "     function()"
            "         return lane_threadname()"
            "     end)"
            " h = g()"
            " local tn = h[1]"
            " assert(tn == h:get_threadname())"
            " assert(tn == 'user name')"
        };
        S.requireSuccess(_script);
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("set thread name from lane body")
    {
        std::string_view const _script{
            " g = lanes.gen('*',"
            "     function()"
            "         lane_threadname('user name')"
            "         return true"
            "     end)"
            " h = g()"
            " h:join()"
            " assert(h:get_threadname() == 'user name')"
        };
        S.requireSuccess(_script);
    }
}

// #################################################################################################
// #################################################################################################

TEST_CASE("lane.cancel")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

    // need the timers so that there is a lane running on which we can operate
    S.requireSuccess("timer_lane = require 'lanes'.configure{with_timers = true}.timer_lane");
    //  make sure we have the timer lane and its cancel method handy
    S.requireSuccess("assert(timer_lane and timer_lane.cancel)");
    // as well as the fixture module
    S.requireSuccess("fixture = require 'fixture'");

    // ---------------------------------------------------------------------------------------------

    SECTION("cancel operation must be a known string")
    {
        // cancel operation must be a known string
        S.requireFailure("timer_lane:cancel('gleh')");
        S.requireFailure("timer_lane:cancel(function() end)");
        S.requireFailure("timer_lane:cancel({})");
        S.requireFailure("timer_lane:cancel(fixture.newuserdata())");
        S.requireFailure("timer_lane:cancel(fixture.newlightuserdata())");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("cancel doesn't expect additional non-number/bool arguments after the mode")
    {
        S.requireFailure("timer_lane:cancel('soft', 'gleh')");
        S.requireFailure("timer_lane:cancel('soft', function() end)");
        S.requireFailure("timer_lane:cancel('soft', {})");
        S.requireFailure("timer_lane:cancel('soft', fixture.newuserdata())");
        S.requireFailure("timer_lane:cancel('soft', fixture.newlightuserdata())");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("hook-based cancellation expects a number for the count. IOW, a bool is not valid")
    {
        S.requireFailure("timer_lane:cancel('call', true)");
        S.requireFailure("timer_lane:cancel('ret', true)");
        S.requireFailure("timer_lane:cancel('line', true)");
        S.requireFailure("timer_lane:cancel('count', true)");
        S.requireFailure("timer_lane:cancel('all', true)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("non-hook should only have one number after the mode (the timeout), else it means we have a count")
    {
        S.requireFailure("timer_lane:cancel('hard', 10, 10)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("extra arguments are not accepted either")
    {
        S.requireFailure("timer_lane:cancel('hard', 10, true, 10)");
        S.requireFailure("timer_lane:cancel('call', 10, 10, 10)");
        S.requireFailure("timer_lane:cancel('line', 10, 10, true, 10)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("out-of-range hook count is not valid")
    {
        S.requireFailure("timer_lane:cancel('call', -1)");
        S.requireFailure("timer_lane:cancel('call', 0)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("out-of-range duration is not valid")
    {
        S.requireFailure("timer_lane:cancel('soft', -1)");
    }
}

// #################################################################################################
// #################################################################################################

// unfortunately, VS Test adapter does not list individual sections,
// so let's create a separate test case for each file with an ugly macro...

#define MAKE_TEST_CASE(DIR, FILE, CONDITION)\
TEST_CASE("scripted tests." #DIR "." #FILE) \
{ \
    FileRunner _runner(R"(.\unit_tests\scripts)"); \
    _runner.performTest(FileRunnerParam{ #DIR "/" #FILE, TestType::CONDITION }); \
}

MAKE_TEST_CASE(lane, cooperative_shutdown, AssertNoLuaError)
#if LUAJIT_FLAVOR() == 0
// TODO: for some reason, even though we throw as expected, the test fails with LuaJIT. To be investigated
MAKE_TEST_CASE(lane, uncooperative_shutdown, AssertThrows)
#endif // LUAJIT_FLAVOR()
MAKE_TEST_CASE(lane, tasking_basic, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_cancelling, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_comms_criss_cross, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_communications, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_error, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_join_test, AssertNoLuaError)
MAKE_TEST_CASE(lane, tasking_send_receive_code, AssertNoLuaError)
MAKE_TEST_CASE(lane, stdlib_naming, AssertNoLuaError)
MAKE_TEST_CASE(coro, basics, AssertNoLuaError)
#if LUAJIT_FLAVOR() == 0
// TODO: for some reason, the test fails with LuaJIT. To be investigated
MAKE_TEST_CASE(coro, error_handling, AssertNoLuaError)
#endif // LUAJIT_FLAVOR()

/*
TEST_CASE("lanes.scripted tests")
{
    auto const& _testParam = GENERATE(
        FileRunnerParam{ PUC_LUA_ONLY("lane/cooperative_shutdown"), TestType::AssertNoLuaError }, // 0
        FileRunnerParam{ "lane/uncooperative_shutdown", TestType::AssertThrows },
        FileRunnerParam{ "lane/tasking_basic", TestType::AssertNoLuaError }, // 2
        FileRunnerParam{ "lane/tasking_cancelling", TestType::AssertNoLuaError }, // 3
        FileRunnerParam{ "lane/tasking_comms_criss_cross", TestType::AssertNoLuaError }, // 4
        FileRunnerParam{ "lane/tasking_communications", TestType::AssertNoLuaError },
        FileRunnerParam{ "lane/tasking_error", TestType::AssertNoLuaError }, // 6
        FileRunnerParam{ "lane/tasking_join_test", TestType::AssertNoLuaError }, // 7
        FileRunnerParam{ "lane/tasking_send_receive_code", TestType::AssertNoLuaError },
        FileRunnerParam{ "lane/stdlib_naming", TestType::AssertNoLuaError },
        FileRunnerParam{ "coro/basics", TestType::AssertNoLuaError }, // 10
        FileRunnerParam{ "coro/error_handling", TestType::AssertNoLuaError }
    );

    FileRunner _runner(R"(.\unit_tests\scripts)");
    _runner.performTest(_testParam);
}
*/
