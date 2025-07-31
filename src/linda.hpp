#pragma once

#include "cancel.hpp"
#include "deep.hpp"
#include "universe.hpp"

struct Keeper;

// #################################################################################################

DECLARE_UNIQUE_TYPE(LindaGroup, int);

class Linda final
: public DeepPrelude // Deep userdata MUST start with this header
{
    public:

    enum class [[nodiscard]] Status
    {
        Active,
        Cancelled
    };
    using enum Status;

    public:
    Universe* const U{ nullptr }; // the universe this linda belongs to

    private:
    static constexpr size_t kEmbeddedNameLength = 24;
    using EmbeddedName = std::array<char, kEmbeddedNameLength>;
    // depending on the name length, it is either embedded inside the Linda, or allocated separately
    std::variant<std::string_view, EmbeddedName> nameVariant{};
    // counts the keeper operations in progress
    mutable std::atomic<int> keeperOperationCount{};
    lua_Duration wakePeriod{};

    public:
    std::condition_variable readHappened{};
    std::condition_variable writeHappened{};
    KeeperIndex const keeperIndex{ -1 }; // the keeper associated to this linda
    Status cancelStatus{ Status::Active };

    public:
    [[nodiscard]]
    static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internalAllocator.alloc(size_); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internalAllocator.free(p_, sizeof(Linda)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Linda*>(p_)->U->internalAllocator.free(p_, sizeof(Linda)); }

    ~Linda();
    Linda(Universe* U_, std::string_view const& name_, lua_Duration wake_period_, LindaGroup group_);
    Linda() = delete;
    // non-copyable, non-movable
    Linda(Linda const&) = delete;
    Linda(Linda const&&) = delete;
    Linda& operator=(Linda const&) = delete;
    Linda& operator=(Linda const&&) = delete;

    private:
    [[nodiscard]]
    static Linda* CreateTimerLinda(lua_State* L_);
    static void DeleteTimerLinda(lua_State* L_, Linda* linda_);
    void freeAllocatedName();
    void setName(std::string_view const& name_);

    public:
    [[nodiscard]]
    Keeper* acquireKeeper() const;
    [[nodiscard]]
    static Linda* CreateTimerLinda(lua_State* const L_, Passkey<Universe> const) { return CreateTimerLinda(L_); }
    static void DeleteTimerLinda(lua_State* const L_, Linda* const linda_, Passkey<Universe> const) { DeleteTimerLinda(L_, linda_); }
    [[nodiscard]]
    std::string_view getName() const;
    [[nodiscard]]
    auto getWakePeriod() const { return wakePeriod; }
    [[nodiscard]]
    bool inKeeperOperation() const { return keeperOperationCount.load(std::memory_order_seq_cst) != 0; }
    template <typename T = uintptr_t>
    [[nodiscard]]
    T obfuscated() const
    {
        // xxh64 of string "kObfuscator" generated at https://www.pelock.com/products/hash-calculator
        static constexpr UniqueKey kObfuscator{ 0x7B8AA1F99A3BD782ull };
        return std::bit_cast<T>(std::bit_cast<uintptr_t>(this) ^ kObfuscator.storage);
    };
    void releaseKeeper(Keeper* keeper_) const;
    [[nodiscard]]
    static int ProtectedCall(lua_State* L_, lua_CFunction f_);
    void pushCancelString(lua_State* L_) const;
    [[nodiscard]]
    Keeper* whichKeeper() const { return U->keepers.getKeeper(keeperIndex); }
};
