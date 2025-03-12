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

    LuaState(LuaState const&) = delete;
    LuaState(LuaState&& rhs_) noexcept
    : L{ std::exchange(rhs_.L, nullptr) }
    {
    }
    LuaState& operator=(LuaState const&) = delete;
    LuaState& operator=(LuaState&& rhs_) noexcept {
        L = std::exchange(rhs_.L, nullptr);
        return *this;
    }

    public:

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
    void requireFailure(std::string_view const& script_);
    void requireNotReturnedString(std::string_view const& script_, std::string_view const& unexpected_);
    void requireReturnedString(std::string_view const& script_, std::string_view const& expected_);
    void requireSuccess(std::string_view const& script_);
    void requireSuccess(std::filesystem::path const& root_, std::string_view const& path_);
    [[nodiscard]]
    LuaError runChunk() const;

    friend std::ostream& operator<<(std::ostream& os_, LuaState const& s_)
    {
        os_ << luaG_tostring(s_.L, kIdxTop);
        return os_;
    }
};

// #################################################################################################

enum class [[nodiscard]] TestType
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

// Define a specialization for FileRunnerParam in Catch::Detail::stringify
namespace Catch {
    namespace Detail {
        template <>
        inline std::string stringify(FileRunnerParam const& param_)
        {
            return std::string{ param_.script };
        }
    } // namespace Detail
} // namespace Catch

class FileRunner : private LuaState
{
    private:

    std::string root;

    public:

    FileRunner(std::string_view const& where_);

    void performTest(FileRunnerParam const& testParam_);
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
