#pragma once

#include "lanesconf.h"
#include "luaerrors.h"

// #################################################################################################

#if HAVE_LUA_ASSERT()

inline void LUA_ASSERT_IMPL(lua_State* L_, bool cond_, char const* file_, int const line_, char const* txt_)
{
    if (!cond_) {
        raise_luaL_error(L_, "LUA_ASSERT %s:%d '%s'", file_, line_, txt_);
    }
}

#define LUA_ASSERT(L_, cond_) LUA_ASSERT_IMPL(L_, cond_, __FILE__, __LINE__, #cond_)

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

    StackChecker(lua_State* const L_, Relative offset_, char const* file_, size_t const line_)
    : L{ L_ }
    , oldtop{ lua_gettop(L_) - offset_ }
    {
        if ((offset_ < 0) || (oldtop < 0)) {
            assert(false);
            raise_luaL_error(L, "STACK INIT ASSERT failed (%d not %d): %s:%llu", lua_gettop(L), offset_, file_, line_);
        }
    }

    StackChecker(lua_State* const L_, Absolute pos_, char const* file_, size_t const line_)
    : L{ L_ }
    , oldtop{ 0 }
    {
        if (lua_gettop(L) != pos_) {
            assert(false);
            raise_luaL_error(L, "STACK INIT ASSERT failed (%d not %d): %s:%llu", lua_gettop(L), pos_, file_, line_);
        }
    }

    StackChecker& operator=(StackChecker const& rhs_)
    {
        assert(L == rhs_.L);
        oldtop = rhs_.oldtop;
        return *this;
    }

    // verify if the distance between the current top and the initial one is what we expect
    void check(int expected_, char const* file_, size_t const line_)
    {
        if (expected_ != LUA_MULTRET) {
            int const actual{ lua_gettop(L) - oldtop };
            if (actual != expected_) {
                assert(false);
                raise_luaL_error(L, "STACK ASSERT failed (%d not %d): %s:%llu", actual, expected_, file_, line_);
            }
        }
    }
};

#define STACK_CHECK_START_REL(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_START_ABS(L, offset_) \
    StackChecker _stackChecker_##L \
    { \
        L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_RESET_REL(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK_RESET_ABS(L, offset_) \
    _stackChecker_##L = StackChecker \
    { \
        L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__ \
    }
#define STACK_CHECK(L, offset_) _stackChecker_##L.check(offset_, __FILE__, __LINE__)

#else // HAVE_LUA_ASSERT()

#define LUA_ASSERT(L_, c) nullptr // nothing

#define STACK_CHECK_START_REL(L_, offset_)
#define STACK_CHECK_START_ABS(L_, offset_)
#define STACK_CHECK_RESET_REL(L_, offset_)
#define STACK_CHECK_RESET_ABS(L_, offset_)
#define STACK_CHECK(L_, offset_)

#endif // HAVE_LUA_ASSERT()
