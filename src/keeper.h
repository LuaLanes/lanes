#if !defined( __keeper_h__)
#define __keeper_h__ 1

struct s_Keeper
{
	MUTEX_T lock_;
	lua_State *L;
	//int count;
};

char const* init_keepers( int const _nbKeepers, lua_CFunction _on_state_create);
void populate_keepers( lua_State *L);
struct s_Keeper *keeper_acquire( const void *ptr);
void keeper_release( struct s_Keeper *K);
void keeper_toggle_nil_sentinels( lua_State *L, int _val_i, int _nil_to_sentinel);
int keeper_call( lua_State *K, char const *func_name, lua_State *L, void *linda, uint_t starting_index);


#endif // __keeper_h__