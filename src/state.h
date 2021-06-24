#ifndef __LANES_STATE_H__
#define __LANES_STATE_H__

//#include "lauxlib.h"
#include "threading.h"
#include "deep.h"

#include "macros_and_utils.h"

void serialize_require( DEBUGSPEW_PARAM_COMMA( Universe* U) lua_State *L);

// ################################################################################################

lua_State* create_state( Universe* U, lua_State* from_);
lua_State* luaG_newstate( Universe* U, lua_State* _from, char const* libs);

// ################################################################################################

void initialize_on_state_create( Universe* U, lua_State* L);
void call_on_state_create( Universe* U, lua_State* L, lua_State* from_, LookupMode mode_);

#endif // __LANES_STATE_H__
