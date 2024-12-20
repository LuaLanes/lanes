#include "_pch.hpp"
#include "shared.h"

#define RUN_LEGACY_TESTS 1
#if RUN_LEGACY_TESTS

// #################################################################################################
// #################################################################################################

TEST_CASE("legacy scripted tests")
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

    FileRunner _runner(R"(.\lanes\tests\)");
    _runner.performTest(_testParam);
}

#endif // RUN_LEGACY_TESTS