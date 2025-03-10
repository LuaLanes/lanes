#include "_pch.hpp"
#include "shared.h"

// #################################################################################################

TEST_CASE("lanes.require 'lanes'")
{
    LuaState L{ LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } };

    // no base library loaded means no print()
    L.requireFailure("print('hello')");

    // need require() to require lanes
    luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 0);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no base library loaded means lanes should issue an error
    L.requireFailure("require 'lanes'");

    // need base to make lanes happy
    luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no table library loaded means lanes should issue an error
    L.requireFailure("require 'lanes'");

    // need table to make lanes happy
    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // no string library loaded means lanes should issue an error
    L.requireFailure("require 'lanes'");

    // need string to make lanes happy
    luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
    lua_pop(L, 1);
    L.stackCheck(0);

    // all required libraries are here: we should be happy
    // that's only the case for Lua > 5.1 though, because the latter can't require() a module after a previously failed attempt (like we just did)
    if constexpr (LUA_VERSION_NUM > 501) {
        L.requireSuccess("require 'lanes'");
    } else {
        // so let's do a fresh attempt in a virgin state where we have the 3 base libraries we need (plus 'package' to be able to require it of course)
        LuaState L51{ LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } };
        luaL_requiref(L51, LUA_LOADLIBNAME, luaopen_package, 1);
        luaL_requiref(L51, LUA_GNAME, luaopen_base, 1);
        luaL_requiref(L51, LUA_TABLIBNAME, luaopen_table, 1);
        luaL_requiref(L51, LUA_STRLIBNAME, luaopen_string, 1);
        lua_settop(L51, 0);
        L51.requireSuccess("require 'lanes'");
    }
}

// #################################################################################################
// #################################################################################################
// allocator should be "protected", a C function returning a suitable userdata, or nil

TEST_CASE("lanes.configure")
{
    LuaState L{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    // ---------------------------------------------------------------------------------------------

    SECTION("allocator", "[allocator]")
    {
        SECTION("allocator = false")
        {
            L.requireFailure("require 'lanes'.configure{allocator = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = true")
        {
            L.requireFailure("require 'lanes'.configure{allocator = true}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <number>")
        {
            L.requireFailure("require 'lanes'.configure{allocator = 33}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <table>")
        {
            L.requireFailure("require 'lanes'.configure{allocator = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <Lua function>")
        {
            L.requireFailure("require 'lanes'.configure{allocator = function() return {}, 12, 'yoy' end}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <bad C function>")
        {
            // a C function that doesn't return what we expect should cause an error too
            L.requireFailure("require 'lanes'.configure{allocator = print}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <string with a typo>")
        {
            // oops, a typo
            L.requireFailure("require 'lanes'.configure{allocator = 'Protected'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = 'protected'")
        {
            // no typo, should work
            L.requireSuccess("require 'lanes'.configure{allocator = 'protected'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator = <good custom C allocator>")
        {
            // a function that provides what we expect is fine
            static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
                lanes::AllocatorDefinition* const _def{ new (L_) lanes::AllocatorDefinition{} };
                _def->initFrom(L_);
                return 1;
            };
            lua_pushcfunction(L, _provideAllocator);
            lua_setglobal(L, "ProvideAllocator");
            L.requireSuccess("require 'lanes'.configure{allocator = ProvideAllocator}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator not returning an AllocatorDefinition")
        {
            // a function that provides something that is definitely not an AllocatorDefinition, should cause an error
            static constexpr lua_CFunction _provideAllocator = +[](lua_State* const L_) {
                lua_newtable(L_);
                return 1;
            };
            lua_pushcfunction(L, _provideAllocator);
            lua_setglobal(L, "ProvideAllocator");
            // force value of internal_allocator so that the LuaJIT-default 'libc' is not selected
            // which would prevent us from calling _provideAllocator
            L.requireFailure("require 'lanes'.configure{allocator = ProvideAllocator, internal_allocator = 'allocator'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator returning an AllocatorDefinition with the wrong signature")
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
            // force value of internal_allocator so that the LuaJIT-default 'libc' is not selected
            // which would prevent us from calling _provideAllocator
            L.requireFailure("require 'lanes'.configure{allocator = ProvideAllocator, internal_allocator = 'allocator'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("allocator returning something too small to be a valid AllocatorDefinition")
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
            // force value of internal_allocator so that the LuaJIT-default 'libc' is not selected
            // which would prevent us from calling _provideAllocator
            L.requireFailure("require 'lanes'.configure{allocator = ProvideAllocator, internal_allocator = 'allocator'}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // internal_allocator should be a string, "libc"/"allocator"

    SECTION("[internal_allocator")
    {
        SECTION("internal_allocator = false")
        {
            L.requireFailure("require 'lanes'.configure{internal_allocator = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = true")
        {
            L.requireFailure("require 'lanes'.configure{internal_allocator = true}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = <table>")
        {
            L.requireFailure("require 'lanes'.configure{internal_allocator = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = <Lua function>")
        {
            L.requireFailure("require 'lanes'.configure{internal_allocator = function() end}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = <bad string>")
        {
            L.requireFailure("require 'lanes'.configure{internal_allocator = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = 'libc'")
        {
            L.requireSuccess("require 'lanes'.configure{internal_allocator = 'libc'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("internal_allocator = 'allocator'")
        {
            L.requireSuccess("require 'lanes'.configure{internal_allocator = 'allocator'}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // keepers_gc_threshold should be a number in [0, 100]

    SECTION("keepers_gc_threshold")
    {
        SECTION("keepers_gc_threshold = <table>")
        {
            L.requireFailure("require 'lanes'.configure{keepers_gc_threshold = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("keepers_gc_threshold = <string>")
        {
            L.requireFailure("require 'lanes'.configure{keepers_gc_threshold = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("keepers_gc_threshold = -1")
        {
            L.requireSuccess("require 'lanes'.configure{keepers_gc_threshold = -1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("keepers_gc_threshold = 0")
        {
            L.requireSuccess("require 'lanes'.configure{keepers_gc_threshold = 0}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("keepers_gc_threshold = 100")
        {
            L.requireSuccess("require 'lanes'.configure{keepers_gc_threshold = 100}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // nb_user_keepers should be a number in [0, 100]

    SECTION("nb_user_keepers")
    {
        SECTION("nb_user_keepers = <table>")
        {
            L.requireFailure("require 'lanes'.configure{nb_user_keepers = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("nb_user_keepers = <string>")
        {
            L.requireFailure("require 'lanes'.configure{nb_user_keepers = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("nb_user_keepers = -1")
        {
            L.requireFailure("require 'lanes'.configure{nb_user_keepers = -1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("nb_user_keepers = 0")
        {
            L.requireSuccess("require 'lanes'.configure{nb_user_keepers = 0}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("nb_user_keepers = 100")
        {
            L.requireSuccess("require 'lanes'.configure{nb_user_keepers = 100}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("nb_user_keepers = 101")
        {
            L.requireFailure("require 'lanes'.configure{nb_user_keepers = 101}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // on_state_create should be a function, either C or Lua, without upvalues

    SECTION("on_state_create")
    {
        SECTION("on_state_create = <table>")
        {
            L.requireFailure("require 'lanes'.configure{on_state_create = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = <string>")
        {
            L.requireFailure("require 'lanes'.configure{on_state_create = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = <number>")
        {
            L.requireFailure("require 'lanes'.configure{on_state_create = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = false")
        {
            L.requireFailure("require 'lanes'.configure{on_state_create = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = true")
        {
            L.requireFailure("require 'lanes'.configure{on_state_create = true}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = <Lua function>")
        {
            // on_state_create isn't called inside a Keeper state if it's a Lua function (which is good as print() doesn't exist there!)
            L.requireSuccess("local print = print; require 'lanes'.configure{on_state_create = function() print 'hello' end}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("on_state_create = <C function>")
        {
            // funnily enough, in Lua 5.3, print() uses global tostring(), that doesn't exist in a keeper since we didn't open libs -> "attempt to call a nil value"
            // conclusion, don't use print() as a fake on_state_create() callback!
            // assert() should be fine since we pass a non-false argument to on_state_create
            L.requireSuccess("require 'lanes'.configure{on_state_create = assert}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // shutdown_timeout should be a number in [0,3600]

    SECTION("shutdown_timeout")
    {
        SECTION("shutdown_timeout = <table>")
        {
            L.requireFailure("require 'lanes'.configure{shutdown_timeout = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = <string>")
        {
            L.requireFailure("require 'lanes'.configure{shutdown_timeout = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = <negative number>")
        {
            L.requireFailure("require 'lanes'.configure{shutdown_timeout = -0.001}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = 0")
        {
            L.requireSuccess("require 'lanes'.configure{shutdown_timeout = 0}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = 1s")
        {
            L.requireSuccess("require 'lanes'.configure{shutdown_timeout = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = 3600s")
        {
            L.requireSuccess("require 'lanes'.configure{shutdown_timeout = 3600}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("shutdown_timeout = <too long>")
        {
            L.requireFailure("require 'lanes'.configure{shutdown_timeout = 3600.001}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // strip_functions should be a boolean

    SECTION("strip_functions")
    {
        SECTION("strip_functions = <table>")
        {
            L.requireFailure("require 'lanes'.configure{strip_functions = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("strip_functions = <string>")
        {
            L.requireFailure("require 'lanes'.configure{strip_functions = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("strip_functions = <number>")
        {
            L.requireFailure("require 'lanes'.configure{strip_functions = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("strip_functions = <C function>")
        {
            L.requireFailure("require 'lanes'.configure{strip_functions = print}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("strip_functions = false")
        {
            L.requireSuccess("require 'lanes'.configure{strip_functions = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("strip_functions = true")
        {
            L.requireSuccess("require 'lanes'.configure{strip_functions = true}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // track_lanes should be a boolean

    SECTION("track_lanes")
    {
        SECTION("track_lanes = <table>")
        {
            L.requireFailure("require 'lanes'.configure{track_lanes = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("track_lanes = <string>")
        {
            L.requireFailure("require 'lanes'.configure{track_lanes = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("track_lanes = <number>")
        {
            L.requireFailure("require 'lanes'.configure{track_lanes = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("track_lanes = <C function>")
        {
            L.requireFailure("require 'lanes'.configure{track_lanes = print}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("track_lanes = false")
        {
            L.requireSuccess("require 'lanes'.configure{track_lanes = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("track_lanes = true")
        {
            L.requireSuccess("require 'lanes'.configure{track_lanes = true}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // verbose_errors should be a boolean

    SECTION("verbose_errors")
    {
        SECTION("verbose_errors = <table>")
        {
            L.requireFailure("require 'lanes'.configure{verbose_errors = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("verbose_errors = <string>")
        {
            L.requireFailure("require 'lanes'.configure{verbose_errors = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("verbose_errors = <number>")
        {
            L.requireFailure("require 'lanes'.configure{verbose_errors = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("verbose_errors = <C function>")
        {
            L.requireFailure("require 'lanes'.configure{verbose_errors = print}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("verbose_errors = false")
        {
            L.requireSuccess("require 'lanes'.configure{verbose_errors = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("verbose_errors = true")
        {
            L.requireSuccess("require 'lanes'.configure{verbose_errors = true}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // with_timers should be a boolean

    SECTION("with_timers")
    {
        SECTION("with_timers = <table>")
        {
            L.requireFailure("require 'lanes'.configure{with_timers = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("with_timers = <string>")
        {
            L.requireFailure("require 'lanes'.configure{with_timers = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("with_timers = <number>")
        {
            L.requireFailure("require 'lanes'.configure{with_timers = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("with_timers = <C function>")
        {
            L.requireFailure("require 'lanes'.configure{with_timers = print}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("with_timers = false")
        {
            L.requireSuccess("require 'lanes'.configure{with_timers = false}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("with_timers = true")
        {
            L.requireSuccess("require 'lanes'.configure{with_timers = true}");
        }
    }

    // ---------------------------------------------------------------------------------------------
    // any unknown setting should be rejected

    SECTION("unknown_setting")
    {
        SECTION("table setting")
        {
            L.requireFailure("require 'lanes'.configure{[{}] = {}}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("boolean setting")
        {
            L.requireFailure("require 'lanes'.configure{[true] = 'gluh'}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("function setting")
        {
            L.requireFailure("require 'lanes'.configure{[function() end] = 1}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("number setting")
        {
            L.requireFailure("require 'lanes'.configure{[1] = function() end}");
        }

        // -----------------------------------------------------------------------------------------

        SECTION("unknown string setting")
        {
            L.requireFailure("require 'lanes'.configure{['gluh'] = false}");
        }
    }
}

// #################################################################################################
// #################################################################################################

#if LUAJIT_FLAVOR() == 0
// TODO: this test crashes inside S.close() against LuaJIT. to be investigated
TEST_CASE("lanes.finally.no fixture")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    // we need Lanes to be up. Since we run several 'scripts', we store it as a global
    S.requireSuccess("lanes = require 'lanes'.configure()");
    // we can set a function
    S.requireSuccess("lanes.finally(function() end)");
    // we can clear it
    S.requireSuccess("lanes.finally(nil)");
    // we can set a new one
    S.requireSuccess("lanes.finally(function() end)");
    // we can replace an existing function
    S.requireSuccess("lanes.finally(error)");
    // even if the finalizer throws a Lua error, it shouldn't crash anything
    REQUIRE_NOTHROW(S.close()); // TODO: use lua_atpanic to catch errors during close()
    REQUIRE_FALSE(S.finalizerWasCalled);
}
#endif // LUAJIT_FLAVOR

// #################################################################################################

TEST_CASE("lanes.finally.with fixture")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };

    // we need Lanes to be up. Since we run several 'scripts', we store it as a global
    S.requireSuccess("lanes = require 'lanes'.configure()");
    // works because we have package.preload.fixture = luaopen_fixture
    S.requireSuccess("fixture = require 'fixture'");
    // set our detectable finalizer
    S.requireSuccess("lanes.finally(fixture.throwing_finalizer)");
    // even if the finalizer can request a C++ exception, it shouldn't do it just now since we have no dangling lane
    REQUIRE_NOTHROW(S.close());
    // the finalizer should be called
    REQUIRE(S.finalizerWasCalled);
}

// #################################################################################################

TEST_CASE("lanes.finally.shutdown with an uncooperative lane")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    S.requireSuccess("lanes = require 'lanes'.configure()");

    // prepare a callback for lanes.finally()
    static bool _wasCalled{};
    static bool _allLanesTerminated{};
    auto _finallyCB{ +[](lua_State* const L_) { _wasCalled = true; _allLanesTerminated = lua_toboolean(L_, 1); return 0; } };
    lua_pushcfunction(S, _finallyCB);
    lua_setglobal(S, "finallyCB");
    // start a lane that lasts a long time
    std::string_view const _script{
        " lanes.finally(finallyCB)"
        " g = lanes.gen('*',"
        "     {name = 'auto'},"
        "     function()"
        "         local f = require 'fixture'"
        "         for i = 1,1e37 do f.give_me_back() end" // no cooperative cancellation checks here, but opportunities for the cancellation hook to trigger
        "     end)"
        " g()"
    };
    S.requireSuccess(_script);
    // close the state before the lane ends.
    // since we don't wait at all, it is possible that the OS thread for the lane hasn't even started at that point
    S.close();
    // the finally handler should have been called, and told all lanes are stopped
    REQUIRE(_wasCalled);
    REQUIRE(_allLanesTerminated);
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

// #################################################################################################

TEST_CASE("lanes.on_state_create setting")
{
    LuaState S{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };

    local::OnStateCreateCallsCount.store(0, std::memory_order_relaxed);

    SECTION("on_state_create called in Keeper states")
    {
        // _G.on_state_create = on_state_create;
        lua_pushcfunction(S, local::on_state_create);
        lua_setglobal(S, "on_state_create");
        S.requireSuccess("lanes = require 'lanes'.configure{on_state_create = on_state_create, nb_user_keepers = 3}");
        REQUIRE(local::OnStateCreateCallsCount.load(std::memory_order_relaxed) == 4);
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("on_state_create called in lane")
    {
        // _G.on_state_create = on_state_create;
        lua_pushcfunction(S, local::on_state_create);
        lua_setglobal(S, "on_state_create");
        S.requireSuccess("lanes = require 'lanes'.configure{on_state_create = on_state_create, with_timers = true}");
        REQUIRE(local::OnStateCreateCallsCount.load(std::memory_order_relaxed) == 2);
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("on_state_create changes package.preload")
    {
        // a C function for which we can test it was called
        static bool _doStuffWasCalled{};
        static auto _doStuff = +[](lua_State* const L_) {
            _doStuffWasCalled = true;
            return 0;
        };

        // a module that exports the above function
        static auto _luaopen_Stuff = +[](lua_State* const L_) {
            lua_newtable(L_); // t
            lua_pushstring(L_, "DoStuffInC"); // t "DoStuffInC"
            lua_pushcfunction(L_, _doStuff); // t "DoStuffInC" _doStuff
            lua_settable(L_, -3); // t
            return 1;
        };

        // a function that installs the module loader function in package.preload
        auto _on_state_create = [](lua_State* const L_) {
            lua_getglobal(L_, "package"); // package
            if (lua_istable(L_, -1)) {
                lua_getfield(L_, -1, "preload"); // package package.preload
                if (lua_istable(L_, -1)) {
                    lua_pushcfunction(L_, _luaopen_Stuff); // package package.preload luaopen_Stuff
                    lua_setfield(L_, -2, "Stuff"); // package package.preload
                }
                lua_pop(L_, 1); // package
            }
            lua_pop(L_, 1); //

            return 0;
        };

        // _G.on_state_create = on_state_create;
        lua_pushcfunction(S, _on_state_create);
        lua_setglobal(S, "on_state_create");

        S.requireSuccess("lanes = require 'lanes'.configure{on_state_create = on_state_create}");

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
        S.requireSuccess(_script);
        REQUIRE(_doStuffWasCalled);
    }
}