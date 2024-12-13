#include "_pch.hpp"
#include "shared.h"

#define RUN_LEGACY_TESTS 1
#if RUN_LEGACY_TESTS

// #################################################################################################

class LegacyTestRunner : public FileRunner
{
    public:
    LegacyTestRunner()
    {
        [[maybe_unused]] std::filesystem::path const _current{ std::filesystem::current_path() };
        std::filesystem::path _path{ R"(.\lanes\tests\)" };
        root = std::filesystem::canonical(_path).generic_string();
        // I need to append that path to the list of locations where modules can be required
        // so that the legacy scripts can require"assert" and find assert.lua
        std::string _script{"package.path = package.path.."};
        _script += "';";
        _script += root;
        _script += "/?.lua'";
        std::ignore = L.doString(_script.c_str());
    }
};

TEST_P(LegacyTestRunner, LegacyTest)
{
    FileRunnerParam const& _param = GetParam();
    switch (_param.test) {
    case TestType::AssertNoLuaError:
        ASSERT_EQ(L.doFile(root, _param.script), LuaError::OK) << L;
        break;
    case TestType::AssertNoThrow:
        ASSERT_NO_THROW((std::ignore = L.doFile(root, _param.script), L.close())) << L;
        break;
    case TestType::AssertThrows:
        ASSERT_THROW((std::ignore = L.doFile(root, _param.script), L.close()), std::logic_error) << L;
        break;
    }
}

INSTANTIATE_TEST_CASE_P(
    LegacyTests,
    LegacyTestRunner,
    ::testing::Values(
        "appendud" // 0
        , "atexit" // 1
        , "atomic" // 2
        , "basic" // 3
        , "cancel" // 4
        , "cyclic" // 5
        , "deadlock" // 6
        , "errhangtest" // 7
        , "error" // 8
        , "fibonacci" // 9
        , "fifo" // 10
        , "finalizer" // 11
        , "func_is_string" // 12
        , "irayo_closure" // 13
        , "irayo_recursive" // 14
        , "keeper" // 15
        //, "linda_perf"
        , LUA54_ONLY("manual_register") // 16: uses lfs module, currently not available for non-5.4 flavors
        , "nameof" // 17
        , "objects" // 18
        , "package" // 19
        , "pingpong" // 20
        , "recursive" // 21
        , "require" // 22
        , "rupval" // 23
        , "timer" // 24
        , LUA54_ONLY("tobeclosed") // 25
        , "track_lanes" // 26
    )
    //, [](::testing::TestParamInfo<FileRunnerParam> const& info_) { return info_.param.script.empty() ? "N/A": std::string{ info_.param.script }; }
);

#endif // RUN_LEGACY_TESTS