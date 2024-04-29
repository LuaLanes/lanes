#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "threading.h"
#include "uniquekey.h"

#include <optional>
#include <mutex>

// forwards
class Linda;
enum class LookupMode;
class Universe;

using KeeperState = Unique<lua_State*>;

struct Keeper
{
    std::mutex m_mutex;
    KeeperState L{ nullptr };
    // int count;
};

struct Keepers
{
    int gc_threshold{ 0 };
    int nb_keepers{ 0 };
    Keeper keeper_array[1];
};

// xxh64 of string "kNilSentinel" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kNilSentinel{ 0xC457D4EDDB05B5E4ull, "lanes.null" };

void init_keepers(Universe* U, lua_State* L);
void close_keepers(Universe* U);

void keeper_toggle_nil_sentinels(lua_State* L, int val_i_, LookupMode const mode_);
[[nodiscard]] int keeper_push_linda_storage(Linda& linda_, DestState L);

using keeper_api_t = lua_CFunction;
#define KEEPER_API(_op) keepercall_##_op
#define PUSH_KEEPER_FUNC lua_pushcfunction
// lua_Cfunctions to run inside a keeper state
[[nodiscard]] int keepercall_clear(lua_State* L);
[[nodiscard]] int keepercall_send(lua_State* L);
[[nodiscard]] int keepercall_receive(lua_State* L);
[[nodiscard]] int keepercall_receive_batched(lua_State* L);
[[nodiscard]] int keepercall_limit(lua_State* L);
[[nodiscard]] int keepercall_get(lua_State* L);
[[nodiscard]] int keepercall_set(lua_State* L);
[[nodiscard]] int keepercall_count(lua_State* L);

using KeeperCallResult = Unique<std::optional<int>>;
[[nodiscard]] KeeperCallResult keeper_call(Universe* U, KeeperState K, keeper_api_t _func, lua_State* L, void* linda, int starting_index);
