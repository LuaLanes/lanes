#pragma once

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include <cassert>

#define USE_DEBUG_SPEW() 0
#if USE_DEBUG_SPEW()
extern char const* debugspew_indent;
#define INDENT_BEGIN "%.*s "
#define INDENT_END , (U ? U->debugspew_indent_depth : 0), debugspew_indent
#define DEBUGSPEW_CODE(_code) _code
#define DEBUGSPEW_PARAM_COMMA( param_) param_,
#define DEBUGSPEW_COMMA_PARAM( param_) , param_
#else // USE_DEBUG_SPEW()
#define DEBUGSPEW_CODE(_code)
#define DEBUGSPEW_PARAM_COMMA( param_)
#define DEBUGSPEW_COMMA_PARAM( param_)
#endif // USE_DEBUG_SPEW()

#ifdef NDEBUG

#define _ASSERT_L(lua,c)     //nothing
#define STACK_DUMP(L)        //nothing

#define STACK_CHECK_START_REL(L, offset_)
#define STACK_CHECK_START_ABS(L, offset_)
#define STACK_CHECK_RESET_REL(L, offset_)
#define STACK_CHECK_RESET_ABS(L, offset_)
#define STACK_CHECK(L, offset_)

#else // NDEBUG

#define _ASSERT_L( L, cond_) if( (cond_) == 0) { (void) luaL_error( L, "ASSERT failed: %s:%d '%s'", __FILE__, __LINE__, #cond_);}
#define STACK_DUMP( L)    luaG_dump( L)

class StackChecker
{
    private:
    lua_State* const m_L;
    int m_oldtop;

    public:
    struct Relative
    {
        int const m_offset;

        operator int() const { return m_offset; }
    };

    struct Absolute
    {
        int const m_offset;

        operator int() const { return m_offset; }
    };

    StackChecker(lua_State* const L_, Relative offset_, char const* file_, size_t const line_)
    : m_L{ L_ }
    , m_oldtop{ lua_gettop(L_) - offset_ }
    {
        if ((offset_ < 0) || (m_oldtop < 0))
        {
            assert(false);
            (void) luaL_error(m_L, "STACK INIT ASSERT failed (%d not %d): %s:%d", lua_gettop(m_L), offset_, file_, line_);
        }
    }

    StackChecker(lua_State* const L_, Absolute pos_, char const* file_, size_t const line_)
    : m_L{ L_ }
    , m_oldtop{ 0 }
    {
        if (lua_gettop(m_L) != pos_)
        {
            assert(false);
            (void) luaL_error(m_L, "STACK INIT ASSERT failed (%d not %d): %s:%d", lua_gettop(m_L), pos_, file_, line_);
        }
    }

    StackChecker& operator=(StackChecker const& rhs_)
    {
        assert(m_L == rhs_.m_L);
        m_oldtop = rhs_.m_oldtop;
        return *this;
    }

    // verify if the distance between the current top and the initial one is what we expect
    void check(int expected_, char const* file_, size_t const line_)
    {
        if (expected_ != LUA_MULTRET)
        {
            int const actual{ lua_gettop(m_L) - m_oldtop };
            if (actual != expected_)
            {
                assert(false);
                luaL_error(m_L, "STACK ASSERT failed (%d not %d): %s:%d", actual, expected_, file_, line_);
            }
        }
    }
};

#define STACK_CHECK_START_REL(L, offset_) StackChecker stackChecker_##L(L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__)
#define STACK_CHECK_START_ABS(L, offset_) StackChecker stackChecker_##L(L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__)
#define STACK_CHECK_RESET_REL(L, offset_) stackChecker_##L = StackChecker{L, StackChecker::Relative{ offset_ }, __FILE__, __LINE__}
#define STACK_CHECK_RESET_ABS(L, offset_) stackChecker_##L = StackChecker{L, StackChecker::Absolute{ offset_ }, __FILE__, __LINE__}
#define STACK_CHECK(L, offset_) stackChecker_##L.check(offset_, __FILE__, __LINE__)

#endif // NDEBUG

#define ASSERT_L(c) _ASSERT_L(L,c)

inline void STACK_GROW(lua_State* L, int n_)
{
    if (!lua_checkstack(L, n_))
        luaL_error(L, "Cannot grow stack!");
}

#define LUAG_FUNC( func_name) int LG_##func_name( lua_State* L)

// a small helper to extract a userdata pointer from the stack
template<typename T>
T* lua_touserdata(lua_State* L, int index_)
{
    return static_cast<T*>(lua_touserdata(L, index_));
}
