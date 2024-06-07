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

#include <atomic>
#include <mutex>
#include <optional>
#include <variant>

// forwards
class Linda;
enum class LookupMode;
class Universe;

using KeeperState = Unique<lua_State*>;

// #################################################################################################

struct Keeper
{
    std::mutex mutex;
    KeeperState K{ nullptr };

    [[nodiscard]] static void* operator new[](size_t size_, Universe* U_) noexcept;
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete[](void* p_, Universe* U_);


    ~Keeper() = default;
    Keeper() = default;
    // non-copyable, non-movable
    Keeper(Keeper const&) = delete;
    Keeper(Keeper const&&) = delete;
    Keeper& operator=(Keeper const&) = delete;
    Keeper& operator=(Keeper const&&) = delete;
};

// #################################################################################################

struct Keepers
{
    private:
    struct DeleteKV
    {
        Universe* U{};
        int count{};
        void operator()(Keeper* k_) const;
    };
    // can't use std::vector<Keeper> because Keeper contains a mutex, so we need a raw memory buffer
    struct KV
    {
        std::unique_ptr<Keeper[], DeleteKV> keepers{};
        size_t nbKeepers{};
    };
    std::variant<std::monostate, Keeper, KV> keeper_array;
    std::atomic_flag isClosing;

    public:
    int gc_threshold{ 0 };

    public:
    // can only be instanced as a data member
    static void* operator new(size_t size_) = delete;

    Keepers() = default;
    void close();
    [[nodiscard]] Keeper* getKeeper(int idx_);
    [[nodiscard]] int getNbKeepers() const;
    void initialize(Universe& U_, lua_State* L_, int nbKeepers_, int gc_threshold_);
};

// #################################################################################################

// xxh64 of string "kNilSentinel" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kNilSentinel{ 0xC457D4EDDB05B5E4ull, "lanes.null" };

[[nodiscard]] int keeper_push_linda_storage(Linda& linda_, DestState L_);

using keeper_api_t = lua_CFunction;
#define KEEPER_API(_op) keepercall_##_op

// lua_Cfunctions to run inside a keeper state
[[nodiscard]] int keepercall_count(lua_State* L_);
[[nodiscard]] int keepercall_destruct(lua_State* L_);
[[nodiscard]] int keepercall_get(lua_State* L_);
[[nodiscard]] int keepercall_limit(lua_State* L_);
[[nodiscard]] int keepercall_receive(lua_State* L_);
[[nodiscard]] int keepercall_receive_batched(lua_State* L_);
[[nodiscard]] int keepercall_send(lua_State* L_);
[[nodiscard]] int keepercall_set(lua_State* L_);

using KeeperCallResult = Unique<std::optional<int>>;
[[nodiscard]] KeeperCallResult keeper_call(KeeperState K_, keeper_api_t func_, lua_State* L_, Linda* linda_, int starting_index_);
