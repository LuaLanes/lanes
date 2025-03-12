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

        // #########################################################################################

        // count the number of live allocations (not the actual byte count, because we can't be sure osize is correct
        std::mutex sAllocStatsLock;
        size_t sAllocCount = 0;
        size_t sAllocBytes = 0;
        std::map<void*, size_t> sAllocs;

        // #########################################################################################

        static void* allocf([[maybe_unused]] void* ud, void* ptr, size_t osize, size_t nsize)
        {
            std::lock_guard guard{ sAllocStatsLock };

            if (!nsize) // free
            {
                if (ptr) {
                    --sAllocCount;
                    sAllocBytes -= osize;
                    sAllocs.erase(ptr);
                    free(ptr);
                }
                return nullptr;
            } else if (!ptr) // malloc
            {
                ++sAllocCount;
                sAllocBytes += nsize;
                void* new_ptr = realloc(ptr, nsize);
                if (new_ptr)
                    sAllocs[new_ptr] = nsize;
                return new_ptr;
            } else // realloc
            {
                int64_t delta = static_cast<int64_t>(nsize) - static_cast<int64_t>(osize);
                sAllocBytes += static_cast<size_t>(delta);
                if (!delta) {
                    return ptr;
                } else {
                    sAllocs.erase(ptr);
                    void* new_ptr = realloc(ptr, nsize);
                    sAllocs[new_ptr] = nsize;
                    return new_ptr;
                }
            }

            // same as lauxlib l_alloc
            //(void)osize;
            // if (nsize == 0)
            //{
            //    free(ptr);
            //    return nullptr;
            //}
            // else
            //    return realloc(ptr, nsize);
        }

        // #########################################################################################

        class EmbeddedLuaState : public LuaState
        {
            private:
            HMODULE hCore{ LoadLibraryW(L"lanes\\core") };
            lua_CFunction lanes_register{};

            public:
            ~EmbeddedLuaState() {
                close();
                FreeLibrary(hCore);
            }
            EmbeddedLuaState()
            : LuaState { LuaState::WithBaseLibs{ false }, LuaState::WithFixture{ false } }
            {

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
                luaL_requiref(L, LUA_GNAME, luaopen_base, 1);
                lua_pop(L, 1);
                stackCheck(0);

                // need package to be able to require lanes
                luaL_requiref(L, LUA_LOADLIBNAME, luaopen_package, 1);
                lua_pop(L, 1);
                stackCheck(0);

                // need table to make lanes happy
                luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
                lua_pop(L, 1);
                stackCheck(0);

                // need string to make lanes happy
                luaL_requiref(L, LUA_STRLIBNAME, luaopen_string, 1);
                lua_pop(L, 1);
                stackCheck(0);

                _p_luaopen_lanes_embedded(L, local::load_lanes_lua); // S: lanes
                lua_pop(L, 1);
                stackCheck(0);
            }

            [[nodiscard]]
            auto get_lanes_register() const noexcept { return lanes_register; }
        };

    } // namespace local
}

// #################################################################################################

TEST_CASE("lanes.embedding.with default allocator")
{
    local::EmbeddedLuaState S;

    // ---------------------------------------------------------------------------------------------

    SECTION("single state")
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
        std::string_view const _script{
            " local lanes = require 'lanes'.configure{with_timers = false}"
            " local l = lanes.linda'gleh'"
            " local upvalue = 'oeauaoeuoeuaoeuaoeujaoefubycfjbycfybcfjybcfjybcfjbcf'"
            " local upvalued = function()"
            "     return upvalue"
            " end"
            " local t = setmetatable({ true, true, true, a = true}, {__index = rawget })"
            " l:set('yo', true, 10, 100.0, upvalue, t, upvalued)" // put a breakpoint in linda_set to have some data to explore with the NATVIS
            " return 'SUCCESS'"
        };
        S.requireReturnedString(_script, "SUCCESS");
    }

    // ---------------------------------------------------------------------------------------------

    SECTION("manual registration")
    {
        S.requireSuccess("require 'lanes'.configure{with_timers = false}");

        // require 'io' library after Lanes is initialized:
        luaL_requiref(S, LUA_IOLIBNAME, luaopen_io, 1);
        lua_pop(S, 1);
        S.stackCheck(0);

        // try to send io.open into a linda, which fails if io base library is not loaded
        std::string_view const _script{
            " local lanes = require 'lanes'"
            " local l = lanes.linda'gleh'"
            " l:set('yo', io.open)"
            " return 'SUCCESS'"
        };
        S.requireNotReturnedString(_script, "SUCCESS");

        // try again after manual registration
        lua_pushcfunction(S, S.get_lanes_register());                                              // S: lanes_register
        luaG_pushstring(S, LUA_IOLIBNAME);                                                         // S: lanes_register "io"
        luaL_requiref(S, LUA_IOLIBNAME, luaopen_io, 1);                                            // S: lanes_register "io" io
        lua_call(S, 2, 0);                                                                         // S:
        S.stackCheck(0);
        S.requireReturnedString(_script, "SUCCESS");
    }
}

// #################################################################################################

// this is not really a test yet, just something sitting here until it is converted properly
TEST_CASE("lanes.embedding.with custom allocator")
{
    static constexpr auto logPrint = +[](lua_State* L) {
        lua_getglobal(L, "ID"); // ID
        printf("[L%d] %s\n", (int) lua_tointeger(L, 2), lua_tostring(L, 1));
        return 0;
    };

    static constexpr auto on_state_create = +[](lua_State* L) {
        lua_pushcfunction(L, logPrint); // logPrint
        lua_setglobal(L, "logPrint"); //
        return 0;
    };

    static constexpr auto launch_lane = +[](lua_CFunction on_state_create_, int id_, int n_) {
        char script[500];
        lua_State* L = lua_newstate(local::allocf, nullptr);
        // _G.ID = id_
        luaL_openlibs(L);
        luaL_dostring(L, "lanes = require 'lanes'");
        lua_getglobal(L, "lanes"); // lanes
        lua_getfield(L, -1, "configure"); // lanes configure
        lua_replace(L, 1); // configure
        lua_newtable(L); // configure {}
        lua_pushcclosure(L, on_state_create_, 0); // configure {} on_state_create_
        lua_setfield(L, -2, "on_state_create"); // configure {on_state_create = on_state_create_}
        lua_pushboolean(L, 0); // configure {on_state_create = on_state_create_} false
        lua_setfield(L, -2, "with_timers"); // configure {on_state_create = on_state_create_, with_timers = false}
        // lua_pushliteral(L, "protected");                                              // configure {on_state_create = on_state_create_} "protected"
        // lua_setfield(L, -2, "allocator");                                             // configure {on_state_create = on_state_create_, allocater = "protected"}
        lua_pcall(L, 1, 0, 0);
        sprintf_s(script,
                  "g = lanes.gen('*', {globals = {ID = %d}}, function(id_) lane_threadname('Lane %d.'..id_) logPrint('This is L%d.'..id_) end)" // lane generator
                  "for i = 1,%d do _G['a'..i] = g(i) end" // launch a few lanes, handle stored in global a<i>
                  ,
                  id_,
                  id_,
                  id_,
                  n_);
        luaL_dostring(L, script); // when script ends, globals are collected, lanes are terminated gracefully
        return L;
    };

    local::EmbeddedLuaState S;

    // ---------------------------------------------------------------------------------------------

    // L1: require 'lanes'.configure{on_state_create = L1_on_state_create, with_timers = false}
    lua_State* const L1 = launch_lane(on_state_create, 1, 5);

    // L2: require 'lanes'.configure{on_state_create = L2_on_state_create, with_timers = false}
    lua_State* const L2 = launch_lane(on_state_create, 2, 5);

    // L3: require 'lanes'.configure{on_state_create = L3_on_state_create, with_timers = false}
    lua_State* const L3 = launch_lane(on_state_create, 3, 5);

    // give some time to the lanes to execute
    std::this_thread::sleep_for(1000ms);

    lua_close(L3);
    lua_close(L2);
    lua_close(L1);
}