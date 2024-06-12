#pragma once

#include "lanesconf.h"
#include "luaerrors.h"

// #################################################################################################

#if HAVE_LUA_ASSERT()

// file name, line number, function name
using SourceLocation = std::tuple<std::string_view, uint_least32_t, std::string_view>;
inline SourceLocation Where(std::source_location const& where_ = std::source_location::current())
{
    std::string_view const _path{ where_.file_name() };
    // drop the directory structure
    std::string_view const _fileName{ _path.substr(_path.find_last_of("\\/")+1) };
    std::string_view const _func{ where_.function_name() };
    return std::make_tuple(_fileName, where_.line(), _func);
}

inline void LUA_ASSERT_IMPL(lua_State* const L_, bool cond_, std::string_view const& txt_, SourceLocation const& where_ = Where())
{
    if (!cond_) {
        raise_luaL_error(L_, "%s:%d: LUA_ASSERT '%s' IN %s", std::get<0>(where_).data(), std::get<1>(where_), txt_.data(), std::get<2>(where_).data());
    }
}

#define LUA_ASSERT(L_, cond_) LUA_ASSERT_IMPL(L_, cond_, #cond_)
#define LUA_ASSERT_CODE(code_) code_

class StackChecker
{
    private:
    lua_State* const L;
    int oldtop;

    public:
    struct Relative
    {
        int const offset;

        operator int() const { return offset; }
    };

    struct Absolute
    {
        int const offset;

        operator int() const { return offset; }
    };

    StackChecker(lua_State* const L_, Relative const offset_, SourceLocation const& where_ = Where())
    : L{ L_ }
    , oldtop{ lua_gettop(L_) - offset_ }
    {
        if ((offset_ < 0) || (oldtop < 0)) {
            assert(false);
            raise_luaL_error(L, "%s:%d: STACK INIT ASSERT (%d not %d) IN %s", std::get<0>(where_).data(), std::get<1>(where_), lua_gettop(L), offset_, std::get<2>(where_).data());
        }
    }

    StackChecker(lua_State* const L_, Absolute const pos_, SourceLocation const& where_ = Where())
    : L{ L_ }
    , oldtop{ 0 }
    {
        if (lua_gettop(L) != pos_) {
            assert(false);
            raise_luaL_error(L, "%s:%d: STACK INIT ASSERT (%d not %d) IN %s", std::get<0>(where_).data(), std::get<1>(where_), lua_gettop(L), pos_, std::get<2>(where_).data());
        }
    }

    StackChecker& operator=(StackChecker const& rhs_)
    {
        assert(L == rhs_.L);
        oldtop = rhs_.oldtop;
        return *this;
    }

    // verify if the distance between the current top and the initial one is what we expect
    void check(int expected_, SourceLocation const& where_ = Where())
    {
        if (expected_ != LUA_MULTRET) {
            int const _actual{ lua_gettop(L) - oldtop };
            if (_actual != expected_) {
                assert(false);
                raise_luaL_error(L, "%s:%d: STACK CHECK ASSERT (%d not %d) IN %s", std::get<0>(where_).data(), std::get<1>(where_), _actual, expected_, std::get<2>(where_).data());
            }
        }
    }
};

#define STACK_CHECK_START_REL(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Relative{ offset_ }, \
    }
#define STACK_CHECK_START_ABS(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Absolute{ offset_ }, \
    }
#define STACK_CHECK_RESET_REL(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Relative{ offset_ }, \
    }
#define STACK_CHECK_RESET_ABS(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Absolute{ offset_ }, \
    }
#define STACK_CHECK(L, offset_) _stackChecker_##L.check(offset_)

#else // HAVE_LUA_ASSERT()

#define LUA_ASSERT(L_, c) nullptr // nothing
#define LUA_ASSERT_CODE(code_) nullptr

#define STACK_CHECK_START_REL(L_, offset_)
#define STACK_CHECK_START_ABS(L_, offset_)
#define STACK_CHECK_RESET_REL(L_, offset_)
#define STACK_CHECK_RESET_ABS(L_, offset_)
#define STACK_CHECK(L_, offset_)

#endif // HAVE_LUA_ASSERT()
