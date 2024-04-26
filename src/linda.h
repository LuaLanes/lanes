#pragma once

#include "cancel.h"
#include "deep.h"
#include "keeper.h"
#include "universe.h"

#include <array>
#include <condition_variable>
#include <variant>

struct Keeper;

// #################################################################################################

using LindaGroup = Unique<int>;

class Linda : public DeepPrelude // Deep userdata MUST start with this header
{
    private:

    static constexpr size_t kEmbeddedNameLength = 24;
    using EmbeddedName = std::array<char, kEmbeddedNameLength>;
    struct AllocatedName
    {
        size_t len{ 0 };
        char* name{ nullptr };
    };
    // depending on the name length, it is either embedded inside the Linda, or allocated separately
    std::variant<AllocatedName, EmbeddedName> m_name;

    public:

    std::condition_variable m_read_happened;
    std::condition_variable m_write_happened;
    Universe* const U{ nullptr }; // the universe this linda belongs to
    int const m_keeper_index{ -1 }; // the keeper associated to this linda
    CancelRequest simulate_cancel{ CancelRequest::None };

    public:

    // a fifo full userdata has one uservalue, the table that holds the actual fifo contents
    [[nodiscard]] static void* operator new(size_t size_, Universe* U_) noexcept { return U_->internal_allocator.alloc(size_); }
    // always embedded somewhere else or "in-place constructed" as a full userdata
    // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
    static void operator delete(void* p_, Universe* U_) { U_->internal_allocator.free(p_, sizeof(Linda)); }
    // this one is for us, to make sure memory is freed by the correct allocator
    static void operator delete(void* p_) { static_cast<Linda*>(p_)->U->internal_allocator.free(p_, sizeof(Linda)); }

    ~Linda();
    Linda(Universe* U_, LindaGroup group_, char const* name_, size_t len_);
    Linda() = delete;
    // non-copyable, non-movable
    Linda(Linda const&) = delete;
    Linda(Linda const&&) = delete;
    Linda& operator=(Linda const&) = delete;
    Linda& operator=(Linda const&&) = delete;

    static int ProtectedCall(lua_State* L, lua_CFunction f_);

    private :

    void setName(char const* name_, size_t len_);

    public:

    [[nodiscard]] char const* getName() const;
    [[nodiscard]] Keeper* whichKeeper() const { return U->keepers->nb_keepers ? &U->keepers->keeper_array[m_keeper_index] : nullptr; }
    [[nodiscard]] Keeper* acquireKeeper() const;
    void releaseKeeper(Keeper* keeper_) const;
};
