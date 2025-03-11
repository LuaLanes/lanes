#include "_pch.hpp"
#include "shared.h"

#define RUN_LEGACY_TESTS 1
#if RUN_LEGACY_TESTS

// #################################################################################################
// #################################################################################################

// unfortunately, VS Test adapter does not list individual sections,
// so let's create a separate test case for each file with an ugly macro...

#define MAKE_TEST_CASE(FILE) \
TEST_CASE("scripted tests.legacy." #FILE) \
{ \
    FileRunner _runner(R"(.\tests\)"); \
    _runner.performTest(FileRunnerParam{ #FILE, TestType::AssertNoLuaError }); \
}

MAKE_TEST_CASE(appendud)
MAKE_TEST_CASE(atexit)
MAKE_TEST_CASE(atomic)
MAKE_TEST_CASE(basic)
MAKE_TEST_CASE(cancel)
MAKE_TEST_CASE(cyclic)
MAKE_TEST_CASE(deadlock)
MAKE_TEST_CASE(errhangtest)
MAKE_TEST_CASE(error)
MAKE_TEST_CASE(fibonacci)
MAKE_TEST_CASE(fifo)
MAKE_TEST_CASE(finalizer)
MAKE_TEST_CASE(func_is_string)
MAKE_TEST_CASE(irayo_closure)
MAKE_TEST_CASE(irayo_recursive)
MAKE_TEST_CASE(keeper)
//MAKE_TEST_CASE(linda_perf)
MAKE_TEST_CASE(manual_register)
MAKE_TEST_CASE(nameof)
MAKE_TEST_CASE(objects)
MAKE_TEST_CASE(package)
MAKE_TEST_CASE(pingpong)
MAKE_TEST_CASE(recursive)
MAKE_TEST_CASE(require)
MAKE_TEST_CASE(rupval)
MAKE_TEST_CASE(timer)
#if LUA_VERSION_NUM == 504
MAKE_TEST_CASE(tobeclosed)
#endif // LUA_VERSION_NUM
MAKE_TEST_CASE(track_lanes)

/*
TEST_CASE("lanes.legacy scripted tests")
{
    auto const& _testParam = GENERATE(
          FileRunnerParam{ "appendud", TestType::AssertNoLuaError } // 0
        , FileRunnerParam{ "atexit", TestType::AssertNoLuaError } // 1
        , FileRunnerParam{ "atomic", TestType::AssertNoLuaError } // 2
        , FileRunnerParam{ "basic", TestType::AssertNoLuaError } // 3
        , FileRunnerParam{ "cancel", TestType::AssertNoLuaError } // 4
        , FileRunnerParam{ "cyclic", TestType::AssertNoLuaError } // 5
        , FileRunnerParam{ "deadlock", TestType::AssertNoLuaError } // 6
        , FileRunnerParam{ "errhangtest", TestType::AssertNoLuaError } // 7
        , FileRunnerParam{ "error", TestType::AssertNoLuaError } // 8
        , FileRunnerParam{ "fibonacci", TestType::AssertNoLuaError } // 9
        , FileRunnerParam{ "fifo", TestType::AssertNoLuaError } // 10
        , FileRunnerParam{ "finalizer", TestType::AssertNoLuaError } // 11
        , FileRunnerParam{ "func_is_string", TestType::AssertNoLuaError } // 12
        , FileRunnerParam{ "irayo_closure", TestType::AssertNoLuaError } // 13
        , FileRunnerParam{ "irayo_recursive", TestType::AssertNoLuaError } // 14
        , FileRunnerParam{ "keeper", TestType::AssertNoLuaError } // 15
      //, FileRunnerParam{ "linda_perf", TestType::AssertNoLuaError }
        , FileRunnerParam{ LUA54_ONLY("manual_register"), TestType::AssertNoLuaError } // 16: uses lfs module, currently not available for non-5.4 flavors
        , FileRunnerParam{ "nameof", TestType::AssertNoLuaError } // 17
        , FileRunnerParam{ "objects", TestType::AssertNoLuaError } // 18
        , FileRunnerParam{ "package", TestType::AssertNoLuaError } // 19
        , FileRunnerParam{ "pingpong", TestType::AssertNoLuaError } // 20
        , FileRunnerParam{ "recursive", TestType::AssertNoLuaError } // 21
        , FileRunnerParam{ "require", TestType::AssertNoLuaError } // 22
        , FileRunnerParam{ "rupval", TestType::AssertNoLuaError } // 23
        , FileRunnerParam{ "timer", TestType::AssertNoLuaError } // 24
        , FileRunnerParam{ LUA54_ONLY("tobeclosed"), TestType::AssertNoLuaError } // 25
        , FileRunnerParam{ "track_lanes", TestType::AssertNoLuaError } // 26
    );

    FileRunner _runner(R"(.\tests\)");
    _runner.performTest(_testParam);
}
*/

#endif // RUN_LEGACY_TESTS