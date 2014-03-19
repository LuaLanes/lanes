/*
* TOOLS.H
*/
#ifndef TOOLS_H
#define TOOLS_H

#include "lauxlib.h"
#include "threading.h"
#include "deep.h"
    // MUTEX_T

#include <assert.h>

// M$ compiler doesn't support 'inline' keyword in C files...
#if defined( _MSC_VER)
#define inline __inline
#endif

// code is now using Lua 5.2 API
// add Lua 5.2 API when building for Lua 5.1
#if LUA_VERSION_NUM == 501
#define lua_absindex( L, idx) (((idx) >= 0 || (idx) <= LUA_REGISTRYINDEX) ? (idx) : lua_gettop(L) + (idx) +1)
#define lua_pushglobaltable(L) lua_pushvalue( L, LUA_GLOBALSINDEX)
#define lua_setuservalue lua_setfenv
#define lua_getuservalue lua_getfenv
#define lua_rawlen lua_objlen
#define luaG_registerlibfuncs( L, _funcs) luaL_register( L, NULL, _funcs)
#define LUA_OK 0
#define LUA_ERRGCMM 666 // doesn't exist in Lua 5.1, we don't care about the actual value
void luaL_requiref (lua_State* L, const char* modname, lua_CFunction openf, int glb); // implementation copied from Lua 5.2 sources
#endif // LUA_VERSION_NUM == 501

// wrap Lua 5.2 calls under Lua 5.1 API when it is simpler that way
#if LUA_VERSION_NUM == 502
#ifndef lua_equal // already defined when compatibility is active in luaconf.h
#define lua_equal( L, a, b) lua_compare( L, a, b, LUA_OPEQ)
#endif // lua_equal
#ifndef lua_lessthan // already defined when compatibility is active in luaconf.h
#define lua_lessthan( L, a, b) lua_compare( L, a, b, LUA_OPLT)
#endif // lua_lessthan
#define luaG_registerlibfuncs( L, _funcs) luaL_setfuncs( L, _funcs, 0)
#endif // LUA_VERSION_NUM == 502

// For some reason, LuaJIT 64bits doesn't support lua_newstate()
// If you build specifically for this situation, change value to 0
#define PROPAGATE_ALLOCF 1
#if PROPAGATE_ALLOCF
#define PROPAGATE_ALLOCF_PREP( L) void* allocUD; lua_Alloc allocF = lua_getallocf( L, &allocUD)
#define PROPAGATE_ALLOCF_ALLOC() lua_newstate( allocF, allocUD)
#else // PROPAGATE_ALLOCF
#define PROPAGATE_ALLOCF_PREP( L)
#define PROPAGATE_ALLOCF_ALLOC() luaL_newstate()
#endif // PROPAGATE_ALLOCF

#define USE_DEBUG_SPEW 0
#if USE_DEBUG_SPEW
extern char const* debugspew_indent;
#define INDENT_BEGIN "%.*s "
#define INDENT_END , (U ? U->debugspew_indent_depth : 0), debugspew_indent
#define DEBUGSPEW_CODE(_code) _code
#else // USE_DEBUG_SPEW
#define DEBUGSPEW_CODE(_code)
#endif // USE_DEBUG_SPEW

// ################################################################################################

/*
 * Do we want to activate full lane tracking feature? (EXPERIMENTAL)
 */
#define HAVE_LANE_TRACKING 1

// ################################################################################################

// this is pointed to by full userdata proxies, and allocated with malloc() to survive any lua_State lifetime
typedef struct
{
	volatile int refcount;
	void* deep;
	// when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the idfunc
	luaG_IdFunction idfunc;
} DEEP_PRELUDE;

// ################################################################################################

// everything regarding the a Lanes universe is stored in that global structure
// held as a full userdata in the master Lua state that required it for the first time
// don't forget to initialize all members in LG_configure()
struct s_Universe
{
	// for verbose errors
	bool_t verboseErrors;

	lua_CFunction on_state_create_func;

	struct s_Keepers* keepers;

	// Initialized by 'init_once_LOCKED()': the deep userdata Linda object
	// used for timers (each lane will get a proxy to this)
	volatile DEEP_PRELUDE* timer_deep;  // = NULL

#if HAVE_LANE_TRACKING
	MUTEX_T tracking_cs;
	struct s_lane* volatile tracking_first; // will change to TRACKING_END if we want to activate tracking
#endif // HAVE_LANE_TRACKING

	MUTEX_T selfdestruct_cs;

	// require() serialization
	MUTEX_T require_cs;

	// Lock for reference counter inc/dec locks (to be initialized by outside code) TODO: get rid of this and use atomics instead!
	MUTEX_T deep_lock; 
	MUTEX_T mtid_lock;

	int last_mt_id;

#if USE_DEBUG_SPEW
	int debugspew_indent_depth;
#endif // USE_DEBUG_SPEW

	struct s_lane* volatile selfdestruct_first;
	// After a lane has removed itself from the chain, it still performs some processing.
	// The terminal desinit sequence should wait for all such processing to terminate before force-killing threads
	int volatile selfdestructing_count;
};

struct s_Universe* get_universe( lua_State* L);
extern void* const UNIVERSE_REGKEY;

// ################################################################################################

#ifdef NDEBUG
  #define _ASSERT_L(lua,c)  /*nothing*/
  #define STACK_CHECK(L)    /*nothing*/
  #define STACK_MID(L,c)    /*nothing*/
  #define STACK_END(L,c)    /*nothing*/
  #define STACK_DUMP(L)    /*nothing*/
#else
  void ASSERT_IMPL( lua_State* L, bool_t cond_, char const* file_, int const line_, char const* text_);
  #define _ASSERT_L(lua,c) ASSERT_IMPL( lua, (c) != 0, __FILE__, __LINE__, #c)
  //
  #define STACK_CHECK(L)     { int const _oldtop_##L = lua_gettop( L)
  #define STACK_MID(L,change) \
	do \
	{ \
		int a = lua_gettop( L) - _oldtop_##L; \
		int b = (change); \
		if( a != b) \
			luaL_error( L, "STACK ASSERT failed (%d not %d): %s:%d", a, b, __FILE__, __LINE__ ); \
	} while( 0)
  #define STACK_END(L,change)  STACK_MID(L,change); }

  #define STACK_DUMP( L)    luaG_dump( L)
#endif
#define ASSERT_L(c) _ASSERT_L(L,c)

#define STACK_GROW(L,n) do { if (!lua_checkstack(L,n)) luaL_error( L, "Cannot grow stack!" ); } while( 0)

#define LUAG_FUNC( func_name ) static int LG_##func_name( lua_State* L)

#define luaG_optunsigned(L,i,d) ((uint_t) luaL_optinteger(L,i,d))
#define luaG_tounsigned(L,i) ((uint_t) lua_tointeger(L,i))

void luaG_dump( lua_State* L );

lua_State* luaG_newstate( struct s_Universe* U, lua_State* _from, char const* libs);
void luaG_copy_one_time_settings( struct s_Universe* U, lua_State* L, lua_State* L2);

// ################################################################################################

enum eLookupMode
{
	eLM_LaneBody, // send the lane body directly from the source to the destination lane
	eLM_ToKeeper, // send a function from a lane to a keeper state
	eLM_FromKeeper // send a function from a keeper state to a lane
};

char const* push_deep_proxy( struct s_Universe* U, lua_State* L, DEEP_PRELUDE* prelude, enum eLookupMode mode_);
void free_deep_prelude( lua_State* L, DEEP_PRELUDE* prelude_);

int luaG_inter_copy_package( struct s_Universe* U, lua_State* L, lua_State* L2, int package_idx_, enum eLookupMode mode_);

int luaG_inter_copy( struct s_Universe* U, lua_State* L, lua_State* L2, uint_t n, enum eLookupMode mode_);
int luaG_inter_move( struct s_Universe* U, lua_State* L, lua_State* L2, uint_t n, enum eLookupMode mode_);

int luaG_nameof( lua_State* L);
int luaG_new_require( lua_State* L);

void populate_func_lookup_table( lua_State* L, int _i, char const* _name);
void serialize_require( struct s_Universe* U, lua_State *L);
void initialize_on_state_create( struct s_Universe* U, lua_State* L);
void call_on_state_create( struct s_Universe* U, lua_State* L, lua_State* from_, enum eLookupMode mode_);

// ################################################################################################

extern char const* const CONFIG_REGKEY;
extern char const* const LOOKUP_REGKEY;

#endif // TOOLS_H
