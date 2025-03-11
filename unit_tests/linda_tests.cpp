#include "_pch.hpp"
#include "shared.h"

// #################################################################################################

TEST_CASE("linda.single Keeper")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'");

    SECTION("Linda creation")
    {
        // no parameters is ok
        S.requireSuccess("lanes.linda()");
        S.requireNotReturnedString("return tostring(lanes.linda())", R"===(Linda: <not a string>)==="); // unspecified name should not result in <not a string>

        // since we have only one keeper, only group 0 is authorized
        S.requireFailure("lanes.linda(-1)");
        S.requireSuccess("lanes.linda(0)");
        S.requireFailure("lanes.linda(1)");

        // any name is ok
        S.requireSuccess("lanes.linda('')"); // an empty name results in a string conversion of the form "Linda: <some hex value>" that we can't test (but it works)
        S.requireReturnedString("return tostring(lanes.linda('short name'))", R"===(Linda: short name)===");
        S.requireReturnedString("return tostring(lanes.linda('very very very very very very long name'))", R"===(Linda: very very very very very very long name)===");
        S.requireReturnedString("return tostring(lanes.linda('auto'))", R"===(Linda: [string "return tostring(lanes.linda('auto'))"]:1)===");

        if constexpr (LUA_VERSION_NUM == 504) {
            // a function is acceptable as a __close handler
            S.requireSuccess("local l <close> = lanes.linda(function() end)");
            // a callable table too (a callable full userdata as well, but I have none here)
            S.requireSuccess("local l <close> = lanes.linda(setmetatable({}, {__call = function() end}))");
            // if the function raises an error, we should get it
            S.requireFailure("local l <close> = lanes.linda(function() error 'gluh' end)");
        } else {
            // no __close support before Lua 5.4
            S.requireFailure("lanes.linda(function() end)");
            S.requireFailure("lanes.linda(setmetatable({}, {__call = function() end}))");
        }

        // mixing parameters in any order is ok: 2 out of 3
        S.requireSuccess("lanes.linda(0, 'name')");
        S.requireSuccess("lanes.linda('name', 0)");
        if constexpr (LUA_VERSION_NUM == 504) {
            S.requireSuccess("lanes.linda(0, function() end)");
            S.requireSuccess("lanes.linda(function() end, 0)");
            S.requireSuccess("lanes.linda('name', function() end)");
            S.requireSuccess("lanes.linda(function() end, 'name')");
        }

        // mixing parameters in any order is ok: 3 out of 3
        if constexpr (LUA_VERSION_NUM == 504) {
            S.requireSuccess("lanes.linda(0, 'name', function() end)");
            S.requireSuccess("lanes.linda(0, function() end, 'name')");
            S.requireSuccess("lanes.linda('name', 0, function() end)");
            S.requireSuccess("lanes.linda('name', function() end, 0)");
            S.requireSuccess("lanes.linda(function() end, 0, 'name')");
            S.requireSuccess("lanes.linda(function() end, 'name', 0)");
        }

        // unsupported parameters should fail
        S.requireFailure("lanes.linda(true)");
        S.requireFailure("lanes.linda(false)");
        // uncallable table or full userdata
        S.requireFailure("lanes.linda({})");
        S.requireFailure("lanes.linda(lanes.linda())");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("Linda indexing")
    {
        // indexing the linda with an unknown string key should fail
        S.requireFailure("return lanes.linda().gouikra");
        // indexing the linda with an unsupported key type should fail
        S.requireFailure("return lanes.linda()[5]");
        S.requireFailure("return lanes.linda()[false]");
        S.requireFailure("return lanes.linda()[{}]");
        S.requireFailure("return lanes.linda()[function() end]");
    }

    // ---------------------------------------------------------------------------------------------
    SECTION("linda:send()")
    {
        SECTION("timeout")
        {
            // timeout checks
            // linda:send() should fail if the timeout is bad
            S.requireFailure("lanes.linda():send(-1, 'k', 'v')");
            // any positive value is ok
            S.requireSuccess("lanes.linda():send(0, 'k', 'v')");
            S.requireSuccess("lanes.linda():send(1e20, 'k', 'v')");
            // nil too (same as 'forever')
            S.requireSuccess("lanes.linda():send(nil, 'k', 'v')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("fails on bad keys")
        {
            // key checks
            // linda:send() should fail if the key is unsupported (nil, table, function, full userdata, reserved light userdata)
            S.requireFailure("lanes.linda():send(0, nil, 'v')");
            S.requireFailure("lanes.linda():send(0, {}, 'v')");
            S.requireFailure("lanes.linda():send(0, function() end, 'v')");
            S.requireFailure("lanes.linda():send(0, io.stdin, 'v')");
            S.requireFailure("lanes.linda():send(0, lanes.null, 'v')");
            S.requireFailure("lanes.linda():send(0, lanes.cancel_error, 'v')");
            S.requireFailure("local l = lanes.linda(); l:send(0, l.batched, 'v')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("succeeds on supported keys")
        {
            // supported keys are ok: boolean, number, string, light userdata, deep userdata
            S.requireSuccess("lanes.linda():send(0, true, 'v')");
            S.requireSuccess("lanes.linda():send(0, false, 'v')");
            S.requireSuccess("lanes.linda():send(0, 99, 'v')");
            S.requireSuccess("local l = lanes.linda(); l:send(0, l:deep(), 'v')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("succeeds on deep userdata key")
        {
            S.requireSuccess("local l = lanes.linda(); l:send(0, l, 'v')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION(". fails")
        {
            // misuse checks, . instead of :
            S.requireFailure("lanes.linda().send(nil, 'k', 'v')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("unsupported values fail")
        {
            // value checks
            // linda:send() should fail if we don't send anything
            S.requireFailure("lanes.linda():send()");
            S.requireFailure("lanes.linda():send(0)");
            S.requireFailure("lanes.linda():send(0, 'k')");
            // or non-deep userdata
            S.requireFailure("lanes.linda():send(0, 'k', fixture.newuserdata())");
            // or something with a converter that raises an error (maybe that should go to a dedicated __lanesconvert test!)
            S.requireFailure("lanes.linda():send(0, 'k', setmetatable({}, {__lanesconvert = function(where_) error (where_ .. ': should not send me' end}))");
            // but a registered non-deep userdata should work
            S.requireSuccess("lanes.linda():send(0, 'k', io.stdin)");
        }
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda::collectgarbage()")
    {
        // linda:collectgarbage() doesn't accept extra arguments
        S.requireFailure("lanes.linda():collectgarbage(true)");
        S.requireSuccess("lanes.linda():collectgarbage()");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda:count()")
    {
        // counting a non-existent key returns nothing
        S.requireSuccess("assert(lanes.linda():count('k') == nil)");
        // counting an existing key returns a correct count
        S.requireSuccess("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1)");
        S.requireSuccess("local l = lanes.linda(); l:set('k', 'a', 'b'); assert(l:count('k') == 2)");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda:limit()")
    {
        SECTION("argument validation")
        {
            // misuse checks, . instead of :
            S.requireFailure("lanes.linda().limit()");

            // not enough keys
            S.requireFailure("lanes.linda():limit()");

            // too many keys?
            S.requireFailure("lanes.linda():limit('k1', 'k2')");
            S.requireFailure("lanes.linda():limit('k1', 'k2', 'k3')");

            // non-numeric limit
            S.requireFailure("lanes.linda():limit('k', false)");
            S.requireFailure("lanes.linda():limit('k', true)");
            S.requireFailure("lanes.linda():limit('k', {})");
            S.requireFailure("lanes.linda():limit('k', lanes.linda():deep())");
            S.requireFailure("lanes.linda():limit('k', assert)");
            S.requireFailure("lanes.linda():limit('k', function() end)");

            // negative limit is forbidden
            S.requireFailure("lanes.linda():limit('k', -1)");

            // we can set a positive limit, or "unlimited"
            S.requireSuccess("lanes.linda():limit('k', 0)");
            S.requireSuccess("lanes.linda():limit('k', 1)");
            S.requireSuccess("lanes.linda():limit('k', 45648946)");
            S.requireSuccess("lanes.linda():limit('k', 'unlimited')");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("normal operations")
        {
            // we can set an inexistent key to unlimited, it should do nothing
            S.requireSuccess("local r,s = lanes.linda():limit('k', 'unlimited'); assert(r==false and s=='under')");
            // reading the limit of an unset key should succeed
            S.requireSuccess("local r,s = lanes.linda():limit('k'); assert(r=='unlimited' and s=='under')");
            // reading the limit after we set one should yield the correct value
            S.requireSuccess("local l = lanes.linda(); local r,s = l:limit('k', 3); assert(r==false and s=='under'); r,s = l:limit('k'); assert(r==3 and s=='under')");
            // changing the limit is possible...
            S.requireSuccess("local l = lanes.linda(); local r,s = l:limit('k', 3); r,s = l:limit('k', 5); r,s = l:limit('k'); assert(r==5 and s=='under', 'b')");
            // ... even if we set a limit below the current count of stored data (which should not change)
            S.requireSuccess("local l = lanes.linda(); local r,s = l:set('k', 'a', 'b', 'c'); assert(r==false and s=='under'); r,s = l:limit('k', 1); assert(r==false and s=='over' and l:count('k') == 3); r,s = l:limit('k'); assert(r==1 and s=='over')");
            // we can remove the limit on a key
            S.requireSuccess("lanes.linda():limit('k', 'unlimited')");

            // emptying a limited key should not remove the limit
            S.requireSuccess("local l = lanes.linda(); l:limit('k', 5); l:set('k'); assert(l:limit('k')==5)");
        }
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda::restrict()")
    {
        // we can read the access restriction of an inexistent Linda, it should tell us there is no restriction
        S.requireSuccess("local r = lanes.linda():restrict('k'); assert(r=='none')");
        // setting an unknown access restriction should fail
        S.requireFailure("lanes.linda():restrict('k', 'gleh')");
        // we can set the access restriction of an inexistent Linda, it should store it and return the previous restriction
        S.requireSuccess("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); local r2 = l:restrict('k'); assert(r1=='none' and r2 == 'set/get')");
        S.requireSuccess("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k'); assert(r1=='none' and r2 == 'send/receive')");

        // we can replace the restriction on a restricted linda
        S.requireSuccess("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k', 'set/get'); assert(r1=='none' and r2 == 'send/receive')");

        // we can remove the restriction on a restricted linda
        S.requireSuccess("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k', 'none'); local r3 = l:restrict('k'); assert(r1=='none' and r2 == 'send/receive' and r3 == 'none')");

        // can't use send/receive on a 'set/get'-restricted key
        S.requireFailure("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); l:send('k', 'bob')");
        S.requireFailure("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); l:receive('k')");
        // can't use get/set on a 'send/receive'-restricted key
        S.requireFailure("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); l:set('k', 'bob')");
        S.requireFailure("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); l:get('k')");

        // emptying a restricted key should not cause the restriction to be forgotten
        S.requireSuccess("local l = lanes.linda(); l:restrict('k', 'set/get'); l:set('k'); assert(l:restrict('k')=='set/get')");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda:set()")
    {
        // we can store more data than the specified limit
        S.requireSuccess("local l = lanes.linda(); l:limit('k', 1); local r,s = l:set('k', 'a', 'b', 'c'); assert(r == false and s == 'over'); assert(l:count('k') == 3)");
        // setting nothing in an inexistent key does not create it
        S.requireSuccess("local l = lanes.linda(); l:set('k'); assert(l:count('k') == nil)");
        // setting a key with some values yields the correct count
        S.requireSuccess("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1) ");
        S.requireSuccess("local l = lanes.linda(); l:limit('k', 1); local r,s = l:set('k', 'a'); assert(r == false and s == 'exact'); assert(l:count('k') == 1)");
        S.requireSuccess("local l = lanes.linda(); l:set('k', 'a', 'b', 'c', 'd'); assert(l:count('k') == 4) ");
        // setting nothing in an existing key removes it ...
        S.requireSuccess("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1); l:set('k'); assert(l:count('k') == nil) ");
        // ... but not if there is a limit (because we don't want to forget it)
        S.requireSuccess("local l = lanes.linda(); l:limit('k', 1); l:set('k', 'a'); l:set('k'); assert(l:count('k') == 0) ");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda:cancel()")
    {
        // unknown linda cancellation mode should raise an error
        S.requireFailure("local l = lanes.linda(); l:cancel('zbougli');");
        // cancelling a linda should change its cancel status to 'cancelled'
        S.requireSuccess("local l = lanes.linda(); l:cancel('read'); assert(l.status == 'cancelled')");
        S.requireSuccess("local l = lanes.linda(); l:cancel('write'); assert(l.status == 'cancelled')");
        S.requireSuccess("local l = lanes.linda(); l:cancel('both'); assert(l.status == 'cancelled')");
        // resetting the linda cancel status
        S.requireSuccess("local l = lanes.linda(); l:cancel('none'); assert(l.status == 'active')");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("linda:wake()")
    {
        // unknown linda wake mode should raise an error
        S.requireFailure("local l = lanes.linda(); l:wake('boulgza');");
        // waking a linda should not change its cancel status
        S.requireSuccess("local l = lanes.linda(); l:wake('read'); assert(l.status == 'active')");
        S.requireSuccess("local l = lanes.linda(); l:wake('write'); assert(l.status == 'active')");
        S.requireSuccess("local l = lanes.linda(); l:wake('both'); assert(l.status == 'active')");
    }
}

// #################################################################################################
// #################################################################################################

TEST_CASE("linda.multi Keeper")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    S.requireSuccess("lanes = require 'lanes'.configure{nb_user_keepers = 3}");

    S.requireFailure("lanes.linda(-1)");
    S.requireSuccess("lanes.linda(0)");
    S.requireSuccess("lanes.linda(1)");
    S.requireSuccess("lanes.linda(2)");
    S.requireSuccess("lanes.linda(3)");
    S.requireFailure("lanes.linda(4)");
}

// #################################################################################################
// #################################################################################################

// unfortunately, VS Test adapter does not list individual sections,
// so let's create a separate test case for each file with an ugly macro...

#define MAKE_TEST_CASE(DIR, FILE) \
TEST_CASE("scripted tests." #DIR "." #FILE) \
{ \
    FileRunner _runner(R"(.\unit_tests\scripts)"); \
    _runner.performTest(FileRunnerParam{ #DIR "/" #FILE, TestType::AssertNoLuaError }); \
}

MAKE_TEST_CASE(linda, send_receive)
MAKE_TEST_CASE(linda, send_registered_userdata)
MAKE_TEST_CASE(linda, multiple_keepers)

/*
TEST_CASE("linda.scripted tests")
{
    auto const& _testParam = GENERATE(
        FileRunnerParam{ "linda/send_receive", TestType::AssertNoLuaError },
        FileRunnerParam{ "linda/send_registered_userdata", TestType::AssertNoLuaError },
        FileRunnerParam{ "linda/multiple_keepers", TestType::AssertNoLuaError }
    );

    FileRunner _runner(R"(.\unit_tests\scripts)");
    _runner.performTest(_testParam);
}
*/