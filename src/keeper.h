#pragma once

#ifdef __cplusplus
extern "C"
{
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

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
    std::mutex mutex;
    KeeperState L{ nullptr };
    // int count;
};

struct Keepers
{
    int gc_threshold{ 0 };
    int nb_keepers{ 0 };
    Keeper keeper_array[1];

    static void CreateFifosTable(lua_State* L_);
};

// xxh64 of string "kNilSentinel" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kNilSentinel{ 0xC457D4EDDB05B5E4ull, "lanes.null" };

void keeper_toggle_nil_sentinels(lua_State* L_, int start_, LookupMode const mode_);
[[nodiscard]] int keeper_push_linda_storage(Linda& linda_, DestState L_);

using keeper_api_t = lua_CFunction;
#define KEEPER_API(_op) keepercall_##_op
#define PUSH_KEEPER_FUNC lua_pushcfunction
// lua_Cfunctions to run inside a keeper state
[[nodiscard]] int keepercall_clear(lua_State* L_);
[[nodiscard]] int keepercall_send(lua_State* L_);
[[nodiscard]] int keepercall_receive(lua_State* L_);
[[nodiscard]] int keepercall_receive_batched(lua_State* L_);
[[nodiscard]] int keepercall_limit(lua_State* L_);
[[nodiscard]] int keepercall_get(lua_State* L_);
[[nodiscard]] int keepercall_set(lua_State* L_);
[[nodiscard]] int keepercall_count(lua_State* L_);

using KeeperCallResult = Unique<std::optional<int>>;
[[nodiscard]] KeeperCallResult keeper_call(KeeperState K_, keeper_api_t func_, lua_State* L_, Linda* linda_, int starting_index_);
