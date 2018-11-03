#ifndef __LANES_TOOLS_H__
#define __LANES_TOOLS_H__

//#include "lauxlib.h"
#include "threading.h"
#include "deep.h"

#include "macros_and_utils.h"

// forwards
struct s_Universe;
typedef struct s_Universe Universe;

// ################################################################################################

#define LUAG_FUNC( func_name ) static int LG_##func_name( lua_State* L)

#define luaG_optunsigned(L,i,d) ((uint_t) luaL_optinteger(L,i,d))
#define luaG_tounsigned(L,i) ((uint_t) lua_tointeger(L,i))

void luaG_dump( lua_State* L );

lua_State* luaG_newstate( Universe* U, lua_State* _from, char const* libs);

// ################################################################################################

int luaG_inter_copy_package( Universe* U, lua_State* L, lua_State* L2, int package_idx_, LookupMode mode_);

int luaG_inter_copy( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_);
int luaG_inter_move( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_);

int luaG_nameof( lua_State* L);
int luaG_new_require( lua_State* L);

void populate_func_lookup_table( lua_State* L, int _i, char const* _name);
void serialize_require( Universe* U, lua_State *L);
void initialize_on_state_create( Universe* U, lua_State* L);
void call_on_state_create( Universe* U, lua_State* L, lua_State* from_, LookupMode mode_);

// ################################################################################################

extern char const* const CONFIG_REGKEY;
extern char const* const LOOKUP_REGKEY;

#endif // __LANES_TOOLS_H__
