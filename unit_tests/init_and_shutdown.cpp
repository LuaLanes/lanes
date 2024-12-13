#include "_pch.hpp"
#include "shared.h"

// #################################################################################################

TEST(Require, MissingBaseLibraries)
{
    LuaState L{ LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } };

    // no base library loaded means no print()
    EXPECT_NE(L.doString("print('hello')"), LuaError::OK);
    L.stackCheck(1);
    lua_pop(L, 1);

    // need require() to require lanes
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 0);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no base library loaded means lanes should issue an error
    EXPECT_NE(L.doString("require 'lanes'"), LuaError::OK);
    lua_pop(L, 1);
    L.stackCheck(0);

    // need base to make lanes happy
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no table library loaded means lanes should issue an error
    EXPECT_NE(L.doString("require 'lanes'"), LuaError::OK);
    L.stackCheck(1);
    lua_pop(L, 1);

    // need table to make lanes happy
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no string library loaded means lanes should issue an error
    EXPECT_NE(L.doString("require 'lanes'"), LuaError::OK);
    lua_pop(L, 1);
    L.stackCheck(0);

    // need string to make lanes happy
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // all required libraries are here: we should be happy
    // that's only the case for Lua > 5.1 though, because the latter can't require() a module after a previously failed attempt (like we just did)
    if constexpr (LUA_VERSION_NUM > 501) {
        ASSERT_EQ(L.doString("require 'lanes'"), LuaError::OK);
    } else {
        // so let's do a fresh attempt in a virgin state where we have the 3 base libraries we need (plus 'package' to be able to require it of course)
        LuaState L51{ LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } };
        luaL_requiref(L51, LUA_LOADLIBNAME, luaopen_package, 1);
        luaL_requiref(L51, LUA_GNAME, luaopen_base, 1);
        luaL_requiref(L51, LUA_TABLIBNAME, luaopen_table, 1);
        luaL_requiref(L51, LUA_STRLIBNAME, luaopen_string, 1);
        lua_settop(L51, 0);
        ASSERT_EQ(L51.doString("require 'lanes'"), LuaError::OK);
    }
}

class Configure : public ::testing::Test
{
    protected:
    LuaState L{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
};

// #################################################################################################
// #################################################################################################
// allocator should be "protected", a C function returning a suitable userdata, or nil

TEST_F(Configure, AllocatorFalse)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorTrue)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = true}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = 33}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorLuaFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = function() return {}, 12, 'yoy' end}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorBadCFunction)
{
    // a C function that doesn't return what we expect should cause an error too
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = print}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorTypo)
{
    // oops, a typo
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = 'Protected'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorProtected)
{
    // no typo, should work
    EXPECT_EQ(L.doString("require 'lanes'.configure{allocator = 'protected'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorCustomOk)
{
    // a function that provides what we expect is fine
    static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
        lanes::AllocatorDefinition* const _def{ new (L_) lanes::AllocatorDefinition{} };
        _def->initFrom(L_);
        return 1;
    };
    lua_pushcfunction(L, _provideAllocator);
    lua_setglobal(L, "ProvideAllocator");
    EXPECT_EQ(L.doString("require 'lanes'.configure{allocator = ProvideAllocator}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorCustomWrongResultType)
{
    // a function that provides something that is definitely not an AllocatorDefinition, should cause an error
    static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
        lua_newtable(L_);
        return 1;
    };
    lua_pushcfunction(L, _provideAllocator);
    lua_setglobal(L, "ProvideAllocator");
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = ProvideAllocator}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorCustomSignatureMismatch)
{
    // a function that provides something that is too small to contain an AllocatorDefinition, should cause an error
    static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
        // create a full userdata that is too small (it only contains enough to store a version tag, but not the rest
        auto* const _duck{ static_cast<lanes::AllocatorDefinition::version_t*>(lua_newuserdata(L_, sizeof(lanes::AllocatorDefinition::version_t))) };
        *_duck = 666777;
        return 1;
    };
    lua_pushcfunction(L, _provideAllocator);
    lua_setglobal(L, "ProvideAllocator");
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = ProvideAllocator}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, AllocatorCustomSizeMismatch)
{
    // a function that provides something that attempts to pass as an AllocatorDefinition, but is not, should cause an error
    static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
        // create a full userdata of the correct size, but of course the contents don't match
        int* const _duck{ static_cast<int*>(lua_newuserdata(L_, sizeof(lanes::AllocatorDefinition))) };
        _duck[0] = 666;
        _duck[1] = 777;
        return 1;
    };
    lua_pushcfunction(L, _provideAllocator);
    lua_setglobal(L, "ProvideAllocator");
    EXPECT_NE(L.doString("require 'lanes'.configure{allocator = ProvideAllocator}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// internal_allocator should be a string, "libc"/"allocator"

TEST_F(Configure, InternalAllocatorFalse)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{internal_allocator = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorTrue)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{internal_allocator = true}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{internal_allocator = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{internal_allocator = function() end}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{internal_allocator = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorLibc)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{internal_allocator = 'libc'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, InternalAllocatorAllocator)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{internal_allocator = 'allocator'}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// keepers_gc_threshold should be a number in [0, 100]

TEST_F(Configure, KeepersGcThresholdTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{keepers_gc_threshold = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, KeepersGcThresholdString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{keepers_gc_threshold = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, KeepersGcThresholdNegative)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{keepers_gc_threshold = -1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, KeepersGcThresholdZero)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{keepers_gc_threshold = 0}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, KeepersGcThresholdHundred)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{keepers_gc_threshold = 100}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// nb_user_keepers should be a number in [0, 100]

TEST_F(Configure, NbUserKeepersTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{nb_user_keepers = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, NbUserKeepersString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{nb_user_keepers = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, NbUserKeepersNegative)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{nb_user_keepers = -1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, NbUserKeepersZero)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{nb_user_keepers = 0}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, NbUserKeepersHundred)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{nb_user_keepers = 100}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, NbUserKeepersHundredAndOne)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{nb_user_keepers = 101}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// on_state_create should be a function, either C or Lua, without upvalues

TEST_F(Configure, OnStateCreateTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{on_state_create = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{on_state_create = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{on_state_create = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateFalse)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{on_state_create = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateTrue)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{on_state_create = true}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateUpvaluedFunction)
{
    // on_state_create isn't called inside a Keeper state if it's a Lua function (which is good as print() doesn't exist there!)
    EXPECT_EQ(L.doString("local print = print; require 'lanes'.configure{on_state_create = function() print 'hello' end}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, OnStateCreateCFunction)
{
    // funnily enough, in Lua 5.3, print() uses global tostring(), that doesn't exist in a keeper since we didn't open libs -> "attempt to call a nil value"
    // conclusion, don't use print() as a fake on_state_create() callback!
    // assert() should be fine since we pass a non-false argument to on_state_create
    EXPECT_EQ(L.doString("require 'lanes'.configure{on_state_create = assert}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// shutdown_timeout should be a number in [0,3600]

TEST_F(Configure, ShutdownTimeoutTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{shutdown_timeout = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{shutdown_timeout = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutNegative)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{shutdown_timeout = -0.001}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutZero)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{shutdown_timeout = 0}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutOne)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{shutdown_timeout = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutHour)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{shutdown_timeout = 3600}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, ShutdownTimeoutTooLong)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{shutdown_timeout = 3600.001}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// strip_functions should be a boolean

TEST_F(Configure, StripFunctionsTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{strip_functions = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, StripFunctionsString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{strip_functions = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, StripFunctionsNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{strip_functions = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, StripFunctionsFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{strip_functions = print}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, StripFunctionsFalse)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{strip_functions = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, StripFunctionsTrue)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{strip_functions = true}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// track_lanes should be a boolean

TEST_F(Configure, TrackLanesTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{track_lanes = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, TrackLanesString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{track_lanes = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, TrackLanesNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{track_lanes = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, TrackLanesFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{track_lanes = print}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, TrackLanesFalse)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{track_lanes = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, TrackLanesTrue)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{track_lanes = true}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// verbose_errors should be a boolean

TEST_F(Configure, VerboseErrorsTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{verbose_errors = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, VerboseErrorsString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{verbose_errors = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, VerboseErrorsNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{verbose_errors = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, VerboseErrorsFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{verbose_errors = print}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, VerboseErrorsFalse)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{verbose_errors = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, VerboseErrorsTrue)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{verbose_errors = true}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// with_timers should be a boolean

TEST_F(Configure, WithTimersTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{with_timers = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, WithTimersString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{with_timers = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, WithTimersNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{with_timers = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, WithTimersFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{with_timers = print}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, WithTimersFalse)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{with_timers = false}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, WithTimersTrue)
{
    EXPECT_EQ(L.doString("require 'lanes'.configure{with_timers = true}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################
// any unknown setting should be rejected

TEST_F(Configure, UnknownSettingTable)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{[{}] = {}}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, UnknownSettingBool)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{[true] = 'gluh'}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, UnknownSettingFunction)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{[function() end] = 1}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, UnknownSettingNumber)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{[1] = function() end}"), LuaError::OK);
}

// #################################################################################################

TEST_F(Configure, UnknownSettingString)
{
    EXPECT_NE(L.doString("require 'lanes'.configure{['gluh'] = false}"), LuaError::OK);
}

// #################################################################################################
// #################################################################################################

TEST(Lanes, Finally)
{
    {
        LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
        // we need Lanes to be up. Since we run several 'scripts', we store it as a global
        EXPECT_EQ(S.doString("lanes = require 'lanes'"), LuaError::OK) << S;
        // we can set a function
        EXPECT_EQ(S.doString("lanes.finally(function() end)"), LuaError::OK) << S;
        // we can clear it
        EXPECT_EQ(S.doString("lanes.finally(nil)"), LuaError::OK) << S;
        // we can set a new one
        EXPECT_EQ(S.doString("lanes.finally(function() end)"), LuaError::OK) << S;
        // we can replace an existing function
        EXPECT_EQ(S.doString("lanes.finally(error)"), LuaError::OK) << S;
        // even if the finalizer throws a Lua error, it shouldn't crash anything
        ASSERT_NO_FATAL_FAILURE(S.close());
        ASSERT_EQ(S.finalizerWasCalled, false);
    }

    {
        LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

        // we need Lanes to be up. Since we run several 'scripts', we store it as a global
        EXPECT_EQ(S.doString("lanes = require 'lanes'"), LuaError::OK) << S;
        // works because we have package.preload.fixture = luaopen_fixture
        EXPECT_EQ(S.doString("fixture = require 'fixture'"), LuaError::OK) << S;
        // set our detectable finalizer
        EXPECT_EQ(S.doString("lanes.finally(fixture.throwing_finalizer)"), LuaError::OK) << S;
        // even if the finalizer can request a C++ exception, it shouldn't do it just now since we have no dangling lane
        ASSERT_NO_THROW(S.close()) << S;
        // the finalizer should be called
        ASSERT_EQ(S.finalizerWasCalled, true);
    }
}

// #################################################################################################

namespace
{
    namespace local
    {
        std::atomic_int OnStateCreateCallsCount{};
        static int on_state_create(lua_State* const L_)
        {
            OnStateCreateCallsCount.fetch_add(1, std::memory_order_relaxed);
            return 0;
        }

    } // namespace local
}

class OnStateCreate : public ::testing::Test
{
    protected:
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    void SetUp() override
    {
        local::OnStateCreateCallsCount.store(0, std::memory_order_relaxed);
    }

    void TearDown() override
    {
        local::OnStateCreateCallsCount.store(0, std::memory_order_relaxed);
    }
};

// #################################################################################################

TEST_F(OnStateCreate, CalledInKeepers)
{
    // _G.on_state_create = on_state_create;
    lua_pushcfunction(S, local::on_state_create);
    lua_setglobal(S, "on_state_create");
    ASSERT_EQ(S.doString("lanes = require 'lanes'.configure{on_state_create = on_state_create, nb_user_keepers = 3}"), LuaError::OK) << S;
    ASSERT_EQ(local::OnStateCreateCallsCount.load(std::memory_order_relaxed), 4) << "on_state_create should have been called once in each Keeper state";
}

// #################################################################################################

TEST_F(OnStateCreate, CalledInLane)
{
    // _G.on_state_create = on_state_create;
    lua_pushcfunction(S, local::on_state_create);
    lua_setglobal(S, "on_state_create");
    ASSERT_EQ(S.doString("lanes = require 'lanes'.configure{on_state_create = on_state_create, with_timers = true}"), LuaError::OK) << S;
    ASSERT_EQ(local::OnStateCreateCallsCount.load(std::memory_order_relaxed), 2) << "on_state_create should have been called in the Keeper state and the timer lane";
}

// #################################################################################################

TEST_F(OnStateCreate, CanPackagePreload)
{
    // a C function for which we can test it was called
    static bool _doStuffWasCalled{};
    static auto _doStuff = +[](lua_State* const L_) {
        _doStuffWasCalled = true;
        return 0;
    };

    // a module that exports the above function
    static auto _luaopen_Stuff = +[](lua_State* const L_) {
        lua_newtable(L_);                                                                          // t
        lua_pushstring(L_, "DoStuffInC");                                                          // t "DoStuffInC"
        lua_pushcfunction(L_, _doStuff);                                                           // t "DoStuffInC" _doStuff
        lua_settable(L_, -3);                                                                      // t
        return 1;
    };

    // a function that installs the module loader function in package.preload
    auto _on_state_create = [](lua_State* const L_) {

        lua_getglobal(L_, "package");                                                              // package
        if (lua_istable(L_, -1)) {
            lua_getfield(L_, -1, "preload");                                                       // package package.preload
            if (lua_istable(L_, -1)) {
                lua_pushcfunction(L_, _luaopen_Stuff);                                             // package package.preload luaopen_Stuff
                lua_setfield(L_, -2, "Stuff");                                                     // package package.preload
            }
            lua_pop(L_, 1);                                                                        // package
        }
        lua_pop(L_, 1);                                                                            //

        return 0;
    };

    // _G.on_state_create = on_state_create;
    lua_pushcfunction(S, _on_state_create);
    lua_setglobal(S, "on_state_create");

    ASSERT_EQ(S.doString("lanes = require 'lanes'.configure{on_state_create = on_state_create}"), LuaError::OK) << S;

    // launch a Lane that requires the module. It should succeed because _on_state_create was called and made it possible
    std::string_view const _script{
        " f = lanes.gen('*',"
        "    function()"
        "        local Stuff = require ('Stuff')"
        "        Stuff.DoStuffInC()"
        "        return true"
        " end)"
        " f():join()" // start the lane and block until it terminates
    };
    ASSERT_EQ(S.doString(_script), LuaError::OK) << S;
    ASSERT_TRUE(_doStuffWasCalled);
}
