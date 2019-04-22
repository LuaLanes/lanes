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

#define luaG_optunsigned(L,i,d) ((uint_t) luaL_optinteger(L,i,d))
#define luaG_tounsigned(L,i) ((uint_t) lua_tointeger(L,i))

#ifdef _DEBUG
void luaG_dump( lua_State* L);
#endif // _DEBUG

lua_State* create_state( Universe* U, lua_State* from_);
lua_State* luaG_newstate( Universe* U, lua_State* _from, char const* libs);

// ################################################################################################

int luaG_inter_copy_package( Universe* U, lua_State* L, lua_State* L2, int package_idx_, LookupMode mode_);

int luaG_inter_copy( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_);
int luaG_inter_move( Universe* U, lua_State* L, lua_State* L2, uint_t n, LookupMode mode_);

int luaG_nameof( lua_State* L);
int luaG_new_require( lua_State* L);

void populate_func_lookup_table( lua_State* L, int _i, char const* _name);
void serialize_require( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State *L);
void initialize_allocator_function( Universe* U, lua_State* L);
void cleanup_allocator_function( Universe* U, lua_State* L);

void initialize_on_state_create( Universe* U, lua_State* L);
void call_on_state_create( Universe* U, lua_State* L, lua_State* from_, LookupMode mode_);

// ################################################################################################

// crc64/we of string "CONFIG_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( CONFIG_REGKEY, 0x31cd24894eae8624); // 'cancel_error' sentinel

// crc64/we of string "LOOKUP_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static DECLARE_CONST_UNIQUE_KEY( LOOKUP_REGKEY, 0x5051ed67ee7b51a1); // 'cancel_error' sentinel

#endif // __LANES_TOOLS_H__
