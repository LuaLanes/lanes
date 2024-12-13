#include "_pch.hpp"
#include "shared.h"

// #################################################################################################

class OneKeeperLindaTests : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

    void SetUp() override
    {
        std::ignore = S.doString("lanes = require 'lanes'");
    }
};

TEST_F(OneKeeperLindaTests, LindaCreation)
{
    // no parameters is ok
    EXPECT_EQ(S.doString("lanes.linda()"), LuaError::OK) << S;
    EXPECT_NE(S.doStringAndRet("return tostring(lanes.linda())"), R"===(Linda: <not a string>)===") << S; // unspecified name should not result in <not a string>

    // since we have only one keeper, only group 0 is authorized
    EXPECT_NE(S.doString("lanes.linda(-1)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.linda(0)"), LuaError::OK) << S;
    EXPECT_NE(S.doString("lanes.linda(1)"), LuaError::OK);

    // any name is ok
    EXPECT_EQ(S.doString("lanes.linda('')"), LuaError::OK) << S; // an empty name results in a string conversion of the form "Linda: <some hex value>" that we can't test (but it works)
    EXPECT_EQ(S.doStringAndRet("return tostring(lanes.linda('short name'))"), R"===(Linda: short name)===") << S;
    EXPECT_EQ(S.doStringAndRet("return tostring(lanes.linda('very very very very very very long name'))"), R"===(Linda: very very very very very very long name)===") << S;
    EXPECT_EQ(S.doStringAndRet("return tostring(lanes.linda('auto'))"), R"===(Linda: [string "return tostring(lanes.linda('auto'))"]:1)===") << S;

    if constexpr (LUA_VERSION_NUM == 504) {
        // a function is acceptable as a __close handler
        EXPECT_EQ(S.doString("local l <close> = lanes.linda(function() end)"), LuaError::OK) << S;
        // a callable table too (a callable full userdata as well, but I have none here)
        EXPECT_EQ(S.doString("local l <close> = lanes.linda(setmetatable({}, {__call = function() end}))"), LuaError::OK) << S;
        // if the function raises an error, we should get it
        EXPECT_NE(S.doString("local l <close> = lanes.linda(function() error 'gluh' end)"), LuaError::OK);
    } else {
        // no __close support before Lua 5.4
        EXPECT_NE(S.doString("lanes.linda(function() end)"), LuaError::OK);
        EXPECT_NE(S.doString("lanes.linda(setmetatable({}, {__call = function() end}))"), LuaError::OK);
    }

    // mixing parameters in any order is ok: 2 out of 3
    EXPECT_EQ(S.doString("lanes.linda(0, 'name')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda('name', 0)"), LuaError::OK) << S;
    if constexpr (LUA_VERSION_NUM == 504) {
        EXPECT_EQ(S.doString("lanes.linda(0, function() end)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda(function() end, 0)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda('name', function() end)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda(function() end, 'name')"), LuaError::OK) << S;
    }

    // mixing parameters in any order is ok: 3 out of 3
    if constexpr (LUA_VERSION_NUM == 504) {
        EXPECT_EQ(S.doString("lanes.linda(0, 'name', function() end)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda(0, function() end, 'name')"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda('name', 0, function() end)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda('name', function() end, 0)"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda(function() end, 0, 'name')"), LuaError::OK) << S;
        EXPECT_EQ(S.doString("lanes.linda(function() end, 'name', 0)"), LuaError::OK) << S;
    }

    // unsupported parameters should fail
    EXPECT_NE(S.doString("lanes.linda(true)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda(false)"), LuaError::OK);
    // uncallable table or full userdata
    EXPECT_NE(S.doString("lanes.linda({})"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda(lanes.linda())"), LuaError::OK);
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaIndexing)
{
    // indexing the linda with an unknown string key should fail
    EXPECT_NE(S.doString("return lanes.linda().gouikra"), LuaError::OK) << S;
    // indexing the linda with an unsupported key type should fail
    EXPECT_NE(S.doString("return lanes.linda()[5]"), LuaError::OK) << S;
    EXPECT_NE(S.doString("return lanes.linda()[false]"), LuaError::OK) << S;
    EXPECT_NE(S.doString("return lanes.linda()[{}]"), LuaError::OK) << S;
    EXPECT_NE(S.doString("return lanes.linda()[function() end]"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendTimeoutValidation)
{
    // timeout checks
    // linda:send() should fail if the timeout is bad
    EXPECT_NE(S.doString("lanes.linda():send(-1, 'k', 'v')"), LuaError::OK);
    // any positive value is ok
    EXPECT_EQ(S.doString("lanes.linda():send(0, 'k', 'v')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():send(1e20, 'k', 'v')"), LuaError::OK) << S;
    // nil too (same as 'forever')
    EXPECT_EQ(S.doString("lanes.linda():send(nil, 'k', 'v')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendFailsOnBadKeys)
{
    // key checks
    // linda:send() should fail if the key is unsupported (nil, table, function, full userdata, reserved light userdata)
    EXPECT_NE(S.doString("lanes.linda():send(0, nil, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, {}, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, function() end, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, io.stdin, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, lanes.null, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, lanes.cancel_error, 'v')"), LuaError::OK);
    EXPECT_NE(S.doString("local l = lanes.linda(); l:send(0, l.batched, 'v')"), LuaError::OK);
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendSucceedsOnSupportedKeys)
{
    // supported keys are ok: boolean, number, string, light userdata, deep userdata
    EXPECT_EQ(S.doString("lanes.linda():send(0, true, 'v')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():send(0, false, 'v')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():send(0, 99, 'v')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:send(0, l:deep(), 'v')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendSucceedsOnDeepUserdataKey)
{
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:send(0, l, 'v')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendSelfValidation)
{
    // misuse checks, . instead of :
    EXPECT_NE(S.doString("lanes.linda().send(nil, 'k', 'v')"), LuaError::OK);
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSendValueValidation)
{
    // value checks
    // linda:send() should fail if we don't send anything
    EXPECT_NE(S.doString("lanes.linda():send()"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():send(0, 'k')"), LuaError::OK);
    // or non-deep userdata
    EXPECT_NE(S.doString("lanes.linda():send(0, 'k', fixture.newuserdata())"), LuaError::OK);
    // or something with a converter that raises an error (maybe that should go to a dedicated __lanesconvert test!)
    EXPECT_NE(S.doString("lanes.linda():send(0, 'k', setmetatable({}, {__lanesconvert = function(where_) error (where_ .. ': should not send me' end}))"), LuaError::OK);
    // but a registered non-deep userdata should work
    EXPECT_EQ(S.doString("lanes.linda():send(0, 'k', io.stdin)"), LuaError::OK);
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaLimitArgumentValidation)
{
    // misuse checks, . instead of :
    EXPECT_NE(S.doString("lanes.linda().limit()"), LuaError::OK);

    // not enough keys
    EXPECT_NE(S.doString("lanes.linda():limit()"), LuaError::OK);

    // too many keys?
    EXPECT_NE(S.doString("lanes.linda():limit('k1', 'k2')"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k1', 'k2', 'k3')"), LuaError::OK);

    // non-numeric limit
    EXPECT_NE(S.doString("lanes.linda():limit('k', false)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k', true)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k', {})"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k', lanes.linda():deep())"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k', assert)"), LuaError::OK);
    EXPECT_NE(S.doString("lanes.linda():limit('k', function() end)"), LuaError::OK);

    // negative limit is forbidden
    EXPECT_NE(S.doString("lanes.linda():limit('k', -1)"), LuaError::OK);

    // we can set a positive limit, or "unlimited"
    EXPECT_EQ(S.doString("lanes.linda():limit('k', 0)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():limit('k', 1)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():limit('k', 45648946)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():limit('k', 'unlimited')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaCollectGarbage)
{
    // linda:collectgarbage() doesn't accept extra arguments
    EXPECT_NE(S.doString("lanes.linda():collectgarbage(true)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda():collectgarbage()"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaCount)
{
    // counting a non-existent key returns nothing
    EXPECT_EQ(S.doString("assert(lanes.linda():count('k') == nil)"), LuaError::OK) << S;
    // counting an existing key returns a correct count
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k', 'a', 'b'); assert(l:count('k') == 2)"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaLimit)
{
    // we can set an inexistent key to unlimited, it should do nothing
    EXPECT_EQ(S.doString("local r,s = lanes.linda():limit('k', 'unlimited'); assert(r==false and s=='under')"), LuaError::OK) << S;
    // reading the limit of an unset key should succeed
    EXPECT_EQ(S.doString("local r,s = lanes.linda():limit('k'); assert(r=='unlimited' and s=='under')"), LuaError::OK) << S;
    // reading the limit after we set one should yield the correct value
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r,s = l:limit('k', 3); assert(r==false and s=='under'); r,s = l:limit('k'); assert(r==3 and s=='under')"), LuaError::OK) << S;
    // changing the limit is possible...
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r,s = l:limit('k', 3); r,s = l:limit('k', 5); r,s = l:limit('k'); assert(r==5 and s=='under', 'b')"), LuaError::OK) << S;
    // ... even if we set a limit below the current count of stored data (which should not change)
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r,s = l:set('k', 'a', 'b', 'c'); assert(r==false and s=='under'); r,s = l:limit('k', 1); assert(r==false and s=='over' and l:count('k') == 3); r,s = l:limit('k'); assert(r==1 and s=='over')"), LuaError::OK) << S;
    // we can remove the limit on a key
    EXPECT_EQ(S.doString("lanes.linda():limit('k', 'unlimited')"), LuaError::OK) << S;

    // emptying a limited key should not remove the limit
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:limit('k', 5); l:set('k'); assert(l:limit('k')==5)"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaRestrict)
{
    // we can read the access restriction of an inexistent Linda, it should tell us there is no restriction
    EXPECT_EQ(S.doString("local r = lanes.linda():restrict('k'); assert(r=='none')"), LuaError::OK) << S;
    // setting an unknown access restriction should fail
    EXPECT_NE(S.doString("lanes.linda():restrict('k', 'gleh')"), LuaError::OK) << S;
    // we can set the access restriction of an inexistent Linda, it should store it and return the previous restriction
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); local r2 = l:restrict('k'); assert(r1=='none' and r2 == 'set/get')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k'); assert(r1=='none' and r2 == 'send/receive')"), LuaError::OK) << S;

    // we can replace the restriction on a restricted linda
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k', 'set/get'); assert(r1=='none' and r2 == 'send/receive')"), LuaError::OK) << S;

    // we can remove the restriction on a restricted linda
    EXPECT_EQ(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); local r2 = l:restrict('k', 'none'); local r3 = l:restrict('k'); assert(r1=='none' and r2 == 'send/receive' and r3 == 'none')"), LuaError::OK) << S;

    // can't use send/receive on a 'set/get'-restricted key
    EXPECT_NE(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); l:send('k', 'bob')"), LuaError::OK) << S;
    EXPECT_NE(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'set/get'); l:receive('k')"), LuaError::OK) << S;
    // can't use get/set on a 'send/receive'-restricted key
    EXPECT_NE(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); l:set('k', 'bob')"), LuaError::OK) << S;
    EXPECT_NE(S.doString("local l = lanes.linda(); local r1 = l:restrict('k', 'send/receive'); l:get('k')"), LuaError::OK) << S;

    // emptying a restricted key should not cause the restriction to be forgotten
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:restrict('k', 'set/get'); l:set('k'); assert(l:restrict('k')=='set/get')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaSet)
{
    // we can store more data than the specified limit
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:limit('k', 1); local r,s = l:set('k', 'a', 'b', 'c'); assert(r == false and s == 'over'); assert(l:count('k') == 3)"), LuaError::OK) << S;
    // setting nothing in an inexistent key does not create it
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k'); assert(l:count('k') == nil)"), LuaError::OK) << S;
    // setting a key with some values yields the correct count
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1) "), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:limit('k', 1); local r,s = l:set('k', 'a'); assert(r == false and s == 'exact'); assert(l:count('k') == 1)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k', 'a', 'b', 'c', 'd'); assert(l:count('k') == 4) "), LuaError::OK) << S;
    // setting nothing in an existing key removes it ...
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:set('k', 'a'); assert(l:count('k') == 1); l:set('k'); assert(l:count('k') == nil) "), LuaError::OK) << S;
    // ... but not if there is a limit (because we don't want to forget it)
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:limit('k', 1); l:set('k', 'a'); l:set('k'); assert(l:count('k') == 0) "), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaCancel)
{
    // unknown linda cancellation mode should raise an error
    EXPECT_NE(S.doString("local l = lanes.linda(); l:cancel('zbougli');"), LuaError::OK) << S;
    // cancelling a linda should change its cancel status to 'cancelled'
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:cancel('read'); assert(l.status == 'cancelled')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:cancel('write'); assert(l.status == 'cancelled')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:cancel('both'); assert(l.status == 'cancelled')"), LuaError::OK) << S;
    // resetting the linda cancel status
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:cancel('none'); assert(l.status == 'active')"), LuaError::OK) << S;
}

// #################################################################################################

TEST_F(OneKeeperLindaTests, LindaWake)
{
    // unknown linda wake mode should raise an error
    EXPECT_NE(S.doString("local l = lanes.linda(); l:wake('boulgza');"), LuaError::OK) << S;
    // waking a linda should not change its cancel status
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:wake('read'); assert(l.status == 'active')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:wake('write'); assert(l.status == 'active')"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("local l = lanes.linda(); l:wake('both'); assert(l.status == 'active')"), LuaError::OK) << S;
}

// #################################################################################################
// #################################################################################################

class MultiKeeperLindaTests : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    void SetUp() override
    {
        std::ignore = S.doString("lanes = require 'lanes'.configure{nb_user_keepers = 3}");
    }
};

TEST_F(MultiKeeperLindaTests, LindaCreation)
{
    EXPECT_NE(S.doString("lanes.linda(-1)"), LuaError::OK);
    EXPECT_EQ(S.doString("lanes.linda(0)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda(1)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda(2)"), LuaError::OK) << S;
    EXPECT_EQ(S.doString("lanes.linda(3)"), LuaError::OK) << S;
    EXPECT_NE(S.doString("lanes.linda(4)"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################

INSTANTIATE_TEST_CASE_P(
    LindaScriptedTests,
    UnitTestRunner,
    ::testing::Values(
        FileRunnerParam{ "linda/send_receive", TestType::AssertNoLuaError },
        FileRunnerParam{ "linda/send_registered_userdata", TestType::AssertNoLuaError },
        FileRunnerParam{ "linda/multiple_keepers", TestType::AssertNoLuaError }
    )
    //,[](::testing::TestParamInfo<FileRunnerParam> const& info_) { return std::string{ info_.param.script }; }
);
