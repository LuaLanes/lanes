#pragma once

// #################################################################################################

class LuaState
{
    public:

    DECLARE_UNIQUE_TYPE(WithBaseLibs, bool);
    DECLARE_UNIQUE_TYPE(WithFixture, bool);
    lua_State* L{ luaL_newstate() };
    bool finalizerWasCalled{};
    STACK_CHECK_START_REL(L, 0);

    ~LuaState()
    {
        close();
    }
    LuaState(WithBaseLibs withBaseLibs_, WithFixture withFixture_);

    LuaState(LuaState&& rhs_) = default;

    operator lua_State*() const { return L; }

    void stackCheck(int delta_) { STACK_CHECK(L, delta_); }
    void close();

    // all these methods leave a single item on the stack: an error string on failure, or a single value that depends on what we do
    [[nodiscard]]
    LuaError doString(std::string_view const& str_) const;
    std::string_view doStringAndRet(std::string_view const& str_) const;
    [[nodiscard]]
    LuaError doFile(std::filesystem::path const& root_, std::string_view const& str_) const;
    [[nodiscard]]
    LuaError loadString(std::string_view const& str_) const;
    [[nodiscard]]
    LuaError loadFile(std::filesystem::path const& root_, std::string_view const& str_) const;
    [[nodiscard]]
    LuaError runChunk() const;

    friend std::ostream& operator<<(std::ostream& os_, LuaState const& s_)
    {
        os_ << luaG_tostring(s_.L, kIdxTop);
        return os_;
    }
};

#define LUA_EXPECT_SUCCESS(S_, WHAT_) { LuaState S{ std::move(S_) }; EXPECT_EQ(S.WHAT_, LuaError::OK) << S; }
#define LUA_EXPECT_FAILURE(S_, WHAT_) { LuaState S{ std::move(S_) }; EXPECT_NE(S.WHAT_, LuaError::OK) << S; }

// #################################################################################################

enum class TestType
{
    AssertNoLuaError,
    AssertNoThrow,
    AssertThrows,
};

struct FileRunnerParam
{
    std::string_view script;
    TestType test;
};

class FileRunner : public ::testing::TestWithParam<FileRunnerParam>
{
    protected:
    LuaState L{ LuaState::WithBaseLibs{ true }, LuaState::WithFixture{ true } };
    std::string root;

    ~FileRunner() override
    {
        lua_settop(L, 0);
    }
    void SetUp() override
    {
        lua_settop(L, 0);
    }

    void TearDown() override
    {
        lua_settop(L, 0);
    }
};

// #################################################################################################

class UnitTestRunner : public FileRunner
{
    public:
    UnitTestRunner();
};

// #################################################################################################

// Can't #ifdef stuff away inside a macro expansion
#if LUA_VERSION_NUM == 501
#define LUA51_ONLY(a) a
#else
#define LUA51_ONLY(a) ""
#endif

#if LUA_VERSION_NUM == 504
#define LUA54_ONLY(a) a
#else
#define LUA54_ONLY(a) ""
#endif

#if LUAJIT_FLAVOR() == 0
#define PUC_LUA_ONLY(a) a
#define JIT_LUA_ONLY(a) ""
#else
#define PUC_LUA_ONLY(a) ""
#define JIT_LUA_ONLY(a) a
#endif
