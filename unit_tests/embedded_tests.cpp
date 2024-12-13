#include "_pch.hpp"
#include "shared.h"
#include "lanes/src/lanes.hpp"

#include <windows.h>

// #################################################################################################

namespace
{
    namespace local
    {
        static int load_lanes_lua(lua_State* const L_)
        {
            if (0 == luaL_dofile(L_, "lanes.lua")) {
                return 1;
            } else {
                return 0;
            }
        }
    } // namespace local
}

// #################################################################################################

class EmbeddedTests : public ::testing::Test
{
    private:
    HMODULE hCore{};

    protected:
    LuaState S{ LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } };
    lua_CFunction lanes_register{};

    void SetUp() override
    {
        hCore = LoadLibraryW(L"lanes\\core");
        if (!hCore) {
            throw std::logic_error("Could not load lanes.core");
        }
        luaopen_lanes_embedded_t const _p_luaopen_lanes_embedded{ reinterpret_cast<luaopen_lanes_embedded_t>(GetProcAddress(hCore, "luaopen_lanes_embedded")) };
        if (!_p_luaopen_lanes_embedded) {
            throw std::logic_error("Could not bind luaopen_lanes_embedded");
        }

        lanes_register = reinterpret_cast<lua_CFunction>(GetProcAddress(hCore, "lanes_register"));
        if (!lanes_register) {
            throw std::logic_error("Could not bind lanes_register");
        }
        // need base to make lanes happy
        luaL_requiref(S, LUA_GNAME, luaopen_base, 1);
        lua_pop(S, 1);
        S.stackCheck(0);

        // need package to be able to require lanes
        luaL_requiref(S, LUA_LOADLIBNAME, luaopen_package, 1);
        lua_pop(S, 1);
        S.stackCheck(0);

        // need table to make lanes happy
        luaL_requiref(S, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(S, 1);
        S.stackCheck(0);

        // need string to make lanes happy
        luaL_requiref(S, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(S, 1);
        S.stackCheck(0);

        _p_luaopen_lanes_embedded(S, local::load_lanes_lua);                                       // S: lanes
        lua_pop(S, 1);
        S.stackCheck(0);
    }

    void TearDown() override
    {
        // close state manually before we unload lanes.core
        S.close();
        FreeLibrary(hCore);
    }
};

TEST_F(EmbeddedTests, SingleState)
{
    // this sends data in a linda. current contents:
    // key: short string
    // values:
    // bool
    // integer
    // number
    // long string
    // table with array and hash parts
    // function with an upvalue
    std::string_view const script{
        "    local lanes = require 'lanes'.configure{with_timers = false}"
        "    local l = lanes.linda'gleh'"
        "    local upvalue = 'oeauaoeuoeuaoeuaoeujaoefubycfjbycfybcfjybcfjybcfjbcf'"
        "    local upvalued = function()"
        "        return upvalue"
        "    end"
        "    local t = setmetatable({ true, true, true, a = true}, {__index = rawget })"
        "    l:set('yo', true, 10, 100.0, upvalue, t, upvalued)" // put a breakpoint in linda_set to have some data to explore with the NATVIS
        "    return 'SUCCESS'"
    };
    std::string_view result = S.doStringAndRet(script);
    ASSERT_EQ(result, "SUCCESS");
}

// #################################################################################################

TEST_F(EmbeddedTests, ManualRegister)
{
    ASSERT_EQ(S.doString("require 'lanes'.configure{with_timers = false}"), LuaError::OK) << S;
    lua_pop(S, 1);

    // require 'io' library after Lanes is initialized:
    luaL_requiref(S, LUA_IOLIBNAME, luaopen_io, 1);
    lua_pop(S, 1);
    S.stackCheck(0);

    // try to send io.open into a linda
    std::string_view const script{
        "local lanes = require 'lanes'.configure{with_timers = false}"
        "local l = lanes.linda'gleh'"
        "l:set('yo', io.open)"
        "return 'SUCCESS'"
    };
    std::string_view const result1{ S.doStringAndRet(script) };
    ASSERT_NE(result1, "SUCCESS") << S;                                                            // S: "msg"
    lua_pop(S, 1);                                                                                 // S:

    // try again after manual registration
    lua_pushcfunction(S, lanes_register);                                                          // S: lanes_register
    luaG_pushstring(S, LUA_IOLIBNAME);                                                             // S: lanes_register "io"
    luaL_requiref(S, LUA_IOLIBNAME, luaopen_io, 1);                                                // S: lanes_register "io" io
    lua_call(S, 2, 0);                                                                             // S:
    S.stackCheck(0);
    std::string_view const result2{ S.doStringAndRet(script) };
    ASSERT_EQ(result2, "SUCCESS") << S;
}