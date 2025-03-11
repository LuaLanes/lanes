#include "_pch.hpp"

#include "shared.h"

// #################################################################################################
// #################################################################################################
// internal fixture module
// #################################################################################################
// #################################################################################################

LANES_API int luaopen_deep_userdata_example(lua_State* L_);

namespace
{
    extern int luaopen_fixture(lua_State*);
    namespace local {
        void PreloadModule(lua_State* const L_, std::string_view const& name_, lua_CFunction const openf_)
        {
            STACK_CHECK_START_REL(L_, 0);
            lua_getglobal(L_, "package");                                                          // L_: package
            std::ignore = luaG_getfield(L_, kIdxTop, "preload");                                   // L_: package package.preload
            lua_pushcfunction(L_, openf_);                                                         // L_: package package.preload openf_
            luaG_setfield(L_, StackIndex{ -2 }, name_);                                            // L_: package package.preload
            lua_pop(L_, 2);
            STACK_CHECK(L_, 0);
        }


        static std::map<lua_State*, std::atomic_flag> sFinalizerHits;
        static std::mutex sCallCountsLock;

        // a finalizer that we can detect even after closing the state
        lua_CFunction sThrowingFinalizer = +[](lua_State* L_) {
            std::lock_guard _guard{ sCallCountsLock };
            sFinalizerHits[L_].test_and_set();
            luaG_pushstring(L_, "throw");
            return 1;
        };

        // a finalizer that we can detect even after closing the state
        lua_CFunction sYieldingFinalizer = +[](lua_State* L_) {
            std::lock_guard _guard{ sCallCountsLock };
            sFinalizerHits[L_].test_and_set();
            return 0;
        };

        // a function that runs forever
        lua_CFunction sForever = +[](lua_State* L_) {
            while (true) {
                std::this_thread::yield();
            }
            return 0;
        };

        // a function that returns immediately (so that LuaJIT issues a function call for it)
        lua_CFunction sGiveMeBack = +[](lua_State* L_) {
            return lua_gettop(L_);
        };

        lua_CFunction sNewLightUserData = +[](lua_State* const L_) {
            lua_pushlightuserdata(L_, std::bit_cast<void*>(static_cast<uintptr_t>(42)));
            return 1;
        };

        lua_CFunction sNewUserData = +[](lua_State* const L_) {
            std::ignore = luaG_newuserdatauv<int>(L_, UserValueCount{ 0 });
            return 1;
        };

        // a function that enables any lane to require "fixture"
        lua_CFunction sOnStateCreate = +[](lua_State* const L_) {
            PreloadModule(L_, "fixture", luaopen_fixture);
            PreloadModule(L_, "deep_userdata_example", luaopen_deep_userdata_example);
            return 0;
        };

        static luaL_Reg const sFixture[] = {
            { "forever", sForever },
            { "give_me_back()", sGiveMeBack },
            { "newlightuserdata", sNewLightUserData },
            { "newuserdata", sNewUserData },
            { "on_state_create", sOnStateCreate },
            { "throwing_finalizer", sThrowingFinalizer },
            { "yielding_finalizer", sYieldingFinalizer },
            { nullptr, nullptr }
        };
    } // namespace local

    // ############################################################################################

    int luaopen_fixture(lua_State* L_)
    {
        STACK_CHECK_START_REL(L_, 0);
        luaG_newlib<std::size(local::sFixture)>(L_, local::sFixture);                              // M
        STACK_CHECK(L_, 1);
        return 1;
    }

} // namespace

// #################################################################################################
// #################################################################################################
// Internals
// #################################################################################################
// #################################################################################################

TEST_CASE("lanes.stack checker")
{
    LuaState _L{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
#if HAVE_LUA_ASSERT()
    StackChecker::CallsCassert = false;
#endif // HAVE_LUA_ASSERT()

    auto _doStackCheckerTest = [&_L](lua_CFunction const _f, LuaError const _expected) {
        lua_pushcfunction(_L, _f);
        REQUIRE(ToLuaError(lua_pcall(_L, 0, 0, 0)) == _expected);
    };

    // function where the StackChecker detects something wrong with the stack
    lua_CFunction _unbalancedStack1 = +[](lua_State* const _L) {
        // record current position
        STACK_CHECK_START_REL(_L, 0);
        // push something
        lua_newtable(_L);
        // check if we are at the same position as before (no)
        STACK_CHECK(_L, 0);
        return 1;
    };

    // function where the StackChecker detects no issue
    lua_CFunction _balancedStack1 = +[](lua_State* const _L) {
        // record current position
        STACK_CHECK_START_REL(_L, 0);
        // check if we are at the same position as before (yes)
        STACK_CHECK(_L, 0);
        return 0;
    };

    lua_CFunction _goodStart = +[](lua_State* const _L) {
        // check that the stack ends at the specified position, and record that as our reference point
        STACK_CHECK_START_ABS(_L, 0);
        // check if we are at the same position as before (yes)
        STACK_CHECK(_L, 0);
        return 0;
    };

    lua_CFunction _badStart = +[](lua_State* const _L) {
        // check that the stack ends at the specified position (no), and record that as our reference point
        STACK_CHECK_START_ABS(_L, 1);
        // check if we are at the same position as before (yes)
        STACK_CHECK(_L, 0);
        return 0;
    };

    _doStackCheckerTest(_unbalancedStack1, LuaError::ERRRUN);
    _doStackCheckerTest(_balancedStack1, LuaError::OK);
    _doStackCheckerTest(_goodStart, LuaError::OK);
    _doStackCheckerTest(_badStart, LuaError::ERRRUN);
}

// #################################################################################################
// #################################################################################################
// LuaState
// #################################################################################################
// #################################################################################################

LuaState::LuaState(WithBaseLibs const withBaseLibs_, WithFixture const withFixture_)
{
    STACK_CHECK_START_REL(L, 0);
    if (withBaseLibs_) {
        luaL_openlibs(L);
    } else {
#if LUAJIT_FLAVOR()
        // lanes.core relies on the presence of jit to detect LuaJIT/PUC-Lua mismatches
        luaL_requiref(L, LUA_JITLIBNAME, luaopen_jit, 1);
        lua_pop(L, 1);
#endif // LUAJIT_FLAVOR
    }
    

    if (withFixture_) {
        // make require "fixture" call luaopen_fixture
        local::PreloadModule(L, "fixture", luaopen_fixture);
        local::PreloadModule(L, "deep_userdata_example", luaopen_deep_userdata_example);
    }
    STACK_CHECK(L, 0);
}

// #################################################################################################

void LuaState::close()
{
    if (L) {
        lua_close(L);

        {
            std::lock_guard _guard{ local::sCallCountsLock };
            finalizerWasCalled = local::sFinalizerHits[L].test();
            local::sFinalizerHits.erase(L);
        }

        L = nullptr;
    }
}

// #################################################################################################

LuaError LuaState::doString(std::string_view const& str_) const
{
    lua_settop(L, 0);
    if (str_.empty()) {
        lua_pushnil(L);
        return LuaError::OK;
    }
    STACK_CHECK_START_REL(L, 0);
    LuaError const _loadErr{ luaL_loadstring(L, str_.data()) };                                    // L: chunk()
    if (_loadErr != LuaError::OK) {
        STACK_CHECK(L, 1); // the error message is on the stack
        return _loadErr;
    }
    LuaError const _callErr{ lua_pcall(L, 0, 1, 0) };                                              // L: "<msg>"?
    [[maybe_unused]] std::string_view const _out{ luaG_tostring(L, kIdxTop) };
    STACK_CHECK(L, 1);
    return _callErr;
}

// #################################################################################################

std::string_view LuaState::doStringAndRet(std::string_view const& str_) const
{
    lua_settop(L, 0);
    if (str_.empty()) {
        luaG_pushstring(L, "");
        return luaG_tostring(L, kIdxTop);
    }
    STACK_CHECK_START_REL(L, 0);
    LuaError const _loadErr{ luaL_loadstring(L, str_.data()) };                                    // L: chunk()
    if (_loadErr != LuaError::OK) {
        STACK_CHECK(L, 1); // the error message is on the stack
        return "";
    }
    [[maybe_unused]] LuaError const _callErr{ lua_pcall(L, 0, 1, 0) };                             // L: "<msg>"?|retstring
    STACK_CHECK(L, 1);
    return luaG_tostring(L, kIdxTop);
}

// #################################################################################################

LuaError LuaState::doFile(std::filesystem::path const& root_, std::string_view const& str_) const
{
    lua_settop(L, 0);
    if (str_.empty()) {
        lua_pushnil(L);
        return LuaError::OK;
    }
    STACK_CHECK_START_REL(L, 0);
    std::filesystem::path _combined{ root_ };
    _combined.append(str_);
    _combined.replace_extension(".lua");
    LuaError const _loadErr{ luaL_loadfile(L, _combined.generic_string().c_str()) };               // L: chunk()
    if (_loadErr != LuaError::OK) {
        STACK_CHECK(L, 1);
        return _loadErr;
    }
    LuaError const _callErr{ lua_pcall(L, 0, 1, 0) };                                              // L: "<msg>"?
    STACK_CHECK(L, 1); // either nil, a return value, or an error string
    return _callErr;
}

// #################################################################################################

LuaError LuaState::loadString(std::string_view const& str_) const
{
    lua_settop(L, 0);
    if (str_.empty()) {
        // this particular test is disabled: just create a dummy function that will run without error
        lua_pushcfunction(L, +[](lua_State*){return 0;});
        return LuaError::OK;
    }
    STACK_CHECK_START_REL(L, 0);
    LuaError const _loadErr{ luaL_loadstring(L, str_.data()) };                                    // L: chunk()
    STACK_CHECK(L, 1); // function on success, error string on failure
    return _loadErr;
}

// #################################################################################################

LuaError LuaState::loadFile(std::filesystem::path const& root_, std::string_view const& str_) const
{
    lua_settop(L, 0);
    STACK_CHECK_START_REL(L, 0);
    if (str_.empty()) {
        // this particular test is disabled: just create a dummy function that will run without error
        lua_pushcfunction(L, +[](lua_State*){return 0;});
        return LuaError::OK;
    }

    std::filesystem::path _combined{ root_ };
    _combined.append(str_);
    _combined.replace_extension(".lua");
    LuaError const _loadErr{ luaL_loadfile(L, _combined.generic_string().c_str()) };               // L: chunk()
    STACK_CHECK(L, 1); // function on success, error string on failure
    return _loadErr;
}

// #################################################################################################

void LuaState::requireFailure(std::string_view const& script_)
{
    auto const _result{ doString(script_) };
    if (_result == LuaError::OK) {
        WARN(luaG_tostring(L, kIdxTop));
    }
    REQUIRE(_result != LuaError::OK);
    lua_settop(L, 0);
}

// #################################################################################################

void LuaState::requireNotReturnedString(std::string_view const& script_, std::string_view const& unexpected_)
{
    auto const _result{ doStringAndRet(script_) };
    if (_result == unexpected_) {
        WARN(_result);
    }
    REQUIRE(_result != unexpected_);
    lua_settop(L, 0);
}

// #################################################################################################

void LuaState::requireReturnedString(std::string_view const& script_, std::string_view const& expected_)
{
    auto const _result{ doStringAndRet(script_) };
    if (_result != expected_) {
        WARN(_result);
    }
    REQUIRE(_result == expected_);
    lua_settop(L, 0);
}

// #################################################################################################

void LuaState::requireSuccess(std::string_view const& script_)
{
    auto const _result{ doString(script_) };
    if (_result != LuaError::OK) {
        WARN(luaG_tostring(L, kIdxTop));
    }
    REQUIRE(_result == LuaError::OK);
    lua_settop(L, 0);
}

// #################################################################################################

void LuaState::requireSuccess(std::filesystem::path const& root_, std::string_view const& path_)
{
    auto const _result{ doFile(root_, path_) };
    if (_result != LuaError::OK) {
        WARN(luaG_tostring(L, kIdxTop));
    }
    REQUIRE(_result == LuaError::OK);
    lua_settop(L, 0);
}

// #################################################################################################

LuaError LuaState::runChunk() const
{
    STACK_CHECK_START_ABS(L, 1); // we must start with the chunk on the stack (or an error string if it failed to load)
    LuaError const _callErr{ lua_pcall(L, 0, 1, 0) };                                              // L: "<msg>"?
    STACK_CHECK(L, 1);
    return _callErr;
}

// #################################################################################################
// #################################################################################################

TEST_CASE("LuaState.doString")
{
    LuaState _L{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ false } };
    // if the script fails to load, we should find the error message at the top of the stack
    REQUIRE([&L = _L]() { std::ignore = L.doString("function end"); return lua_gettop(L) == 1 && luaG_type(L, StackIndex{1}) == LuaType::STRING; }());

    // if the script runs, the stack should contain its return value
    REQUIRE([&L = _L]() { std::ignore = L.doString("return true"); return lua_gettop(L) == 1 && luaG_type(L, StackIndex{1}) == LuaType::BOOLEAN; }());
    REQUIRE([&L = _L]() { std::ignore = L.doString("return 'hello'"); return lua_gettop(L) == 1 && luaG_tostring(L, StackIndex{1}) == "hello"; }());
    // or nil if it didn't return anything
    REQUIRE([&L = _L]() { std::ignore = L.doString("return"); return lua_gettop(L) == 1 && luaG_type(L, StackIndex{1}) == LuaType::NIL; }());

    // on failure, doStringAndRet returns "", and the error message is on the stack
    REQUIRE([&L = _L]() { return L.doStringAndRet("function end") == "" && lua_gettop(L) == 1 && luaG_type(L, StackIndex{ 1 }) == LuaType::STRING && luaG_tostring(L, StackIndex{ 1 }) != ""; }());
    // on success doStringAndRet returns the string returned by the script, that is also at the top of the stack
    REQUIRE([&L = _L]() { return L.doStringAndRet("return 'hello'") == "hello" && lua_gettop(L) == 1 && luaG_type(L, StackIndex{ 1 }) == LuaType::STRING && luaG_tostring(L, StackIndex{ 1 }) == "hello"; }());
    // if the returned value is not (convertible to) a string, we should get an empty string out of doStringAndRet
    REQUIRE([&L = _L]() { return L.doStringAndRet("return function() end") == "" && lua_gettop(L) == 1 && luaG_type(L, StackIndex{ 1 }) == LuaType::FUNCTION && luaG_tostring(L, StackIndex{ 1 }) == ""; }());
}

// #################################################################################################
// #################################################################################################
// FileRunner
// #################################################################################################
// #################################################################################################

FileRunner::FileRunner(std::string_view const& where_)
: LuaState{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } }
{
    // _G.print = _nullprint
    // because the VS Test Explorer doesn't appreciate the text output of some scripts, so absorb them
    if constexpr (1) {
        auto const _nullprint = +[](lua_State* const L_) { return 0; };
        luaG_pushglobaltable(L);
        lua_pushcfunction(L, _nullprint);
        luaG_setfield(L, StackIndex{ -2 }, std::string_view{ "print" });
        lua_pop(L, 1);
        stackCheck(0);
    }
    // TODO: do the same with io.stderr:write?

    [[maybe_unused]] std::filesystem::path const _current{ std::filesystem::current_path() };
    std::filesystem::path _path{ where_ };
    root = std::filesystem::canonical(_path).generic_string();
    // I need to append that path to the list of locations where modules can be required
    // so that the legacy scripts can require"assert" and find assert.lua
    std::string _script{ "package.path = package.path.." };
    _script += "';";
    _script += root;
    _script += "/?.lua'";
    std::ignore = doString(_script.c_str());
}

// #################################################################################################

void FileRunner::performTest(FileRunnerParam const& testParam_)
{
    INFO(testParam_.script);
    switch (testParam_.test) {
    case TestType::AssertNoLuaError:
        requireSuccess(root, testParam_.script);
        break;
    case TestType::AssertNoThrow:
        REQUIRE_NOTHROW((std::ignore = doFile(root, testParam_.script), close()));
        break;
    case TestType::AssertThrows:
        REQUIRE_THROWS_AS((std::ignore = doFile(root, testParam_.script), close()), std::logic_error);
        break;
    }
}
