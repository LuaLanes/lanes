#pragma once

#include "cancel.h"
#include "deep.h"
#include "universe.h"

struct Keeper;

// #################################################################################################

// xxh64 of string "kLindaBatched" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kLindaBatched{ 0xB8234DF772646567ull, "linda.batched" };

// #################################################################################################

using LindaGroup = Unique<int>;

class Linda
: public DeepPrelude // Deep userdata MUST start with this header
{
    public:
    class KeeperOperationInProgress
    {
        private:
        Linda& linda;
        [[maybe_unused]] lua_State* const L; // just here for inspection while debugging

        public:
        KeeperOperationInProgress(Linda& linda_, lua_State* const L_)
        : linda{ linda_ }
        , L{ L_ }
        {
            [[maybe_unused]] int const _prev{ linda.keeperOperationCount.fetch_add(1, std::memory_order_seq_cst) };
        }

        public:
        ~KeeperOperationInProgress()
        {
            [[maybe_unused]] int const _prev{ linda.keeperOperationCount.fetch_sub(1, std::memory_order_seq_cst) };
        }
    };

    private:
    static constexpr size_t kEmbeddedNameLength = 24;
    using EmbeddedName = std::array<char, kEmbeddedNameLength>;
    // depending on the name length, it is either embedded inside the Linda, or allocated separately
    std::variant<std::string_view, EmbeddedName> nameVariant{};
    // counts the keeper operations in progress
    std::atomic<int> keeperOperationCount{};

    public:
    std::condition_variable readHappened{};
    std::condition_variable writeHappened{};
    Universe* const U{ nullptr }; // the universe this linda belongs to
    int const keeperIndex{ -1 }; // the keeper associated to this linda
    CancelRequest cancelRequest{ CancelRequest::None };

    public:
    [[nodiscard]] static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internalAllocator.alloc(size_); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internalAllocator.free(p_, sizeof(Linda)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Linda*>(p_)->U->internalAllocator.free(p_, sizeof(Linda)); }

    ~Linda();
    Linda(Universe* U_, LindaGroup group_, std::string_view const& name_);
    Linda() = delete;
    // non-copyable, non-movable
    Linda(Linda const&) = delete;
    Linda(Linda const&&) = delete;
    Linda& operator=(Linda const&) = delete;
    Linda& operator=(Linda const&&) = delete;

    private:
    void freeAllocatedName();
    void setName(std::string_view const& name_);

    public:
    [[nodiscard]] Keeper* acquireKeeper() const;
    [[nodiscard]] std::string_view getName() const;
    [[nodiscard]] bool inKeeperOperation() const { return keeperOperationCount.load(std::memory_order_seq_cst) != 0; }
    template <typename T = uintptr_t>
    [[nodiscard]] T obfuscated() const
    {
        // xxh64 of string "kObfuscator" generated at https://www.pelock.com/products/hash-calculator
        static constexpr UniqueKey kObfuscator{ 0x7B8AA1F99A3BD782ull };
        return std::bit_cast<T>(std::bit_cast<uintptr_t>(this) ^ kObfuscator.storage);
    };
    void releaseKeeper(Keeper* keeper_) const;
    [[nodiscard]] static int ProtectedCall(lua_State* L_, lua_CFunction f_);
    [[nodiscard]] KeeperOperationInProgress startKeeperOperation(lua_State* const L_) { return KeeperOperationInProgress{ *this, L_ }; };
    [[nodiscard]] Keeper* whichKeeper() const { return U->keepers.getKeeper(keeperIndex); }
};
