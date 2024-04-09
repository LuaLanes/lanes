#pragma once

#include "threading.h"
#include "deep.h"

#include "macros_and_utils.h"

// forwards
class Universe;

// ################################################################################################

#ifdef _DEBUG
void luaG_dump( lua_State* L);
#endif // _DEBUG

// ################################################################################################

void push_registry_subtable_mode( lua_State* L, UniqueKey key_, const char* mode_);
void push_registry_subtable( lua_State* L, UniqueKey key_);

enum class VT
{
    NORMAL,
    KEY,
    METATABLE
};
bool inter_copy_one(Universe* U, Dest L2, int L2_cache_i, Source L, int i, VT vt_, LookupMode mode_, char const* upName_);

// ################################################################################################

int luaG_inter_copy_package(Universe* U, Source L, Dest L2, int package_idx_, LookupMode mode_);

int luaG_inter_copy(Universe* U, Source L, Dest L2, int n, LookupMode mode_);
int luaG_inter_move(Universe* U, Source L, Dest L2, int n, LookupMode mode_);

int luaG_nameof(lua_State* L);

void populate_func_lookup_table(lua_State* L, int _i, char const* _name);
void initialize_allocator_function(Universe* U, lua_State* L);

// ################################################################################################

// crc64/we of string "CONFIG_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey CONFIG_REGKEY{ 0x31cd24894eae8624ull }; // registry key to access the configuration

// crc64/we of string "LOOKUP_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey LOOKUP_REGKEY{ 0x5051ed67ee7b51a1ull }; // registry key to access the lookup database
