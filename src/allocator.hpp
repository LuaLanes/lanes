#pragma once

#include "compat.hpp"

// #################################################################################################

namespace lanes {

    // everything we need to provide to lua_newstate()
    class AllocatorDefinition
    {
        private:
        // xxh64 of string "kAllocatorVersion_1" generated at https://www.pelock.com/products/hash-calculator
        static constexpr auto kAllocatorVersion{ static_cast<uintptr_t>(0xCF9D321B0DFB5715ull) };

        public:
        using version_t = std::remove_const_t<decltype(kAllocatorVersion)>;

        private:
        // can't make these members const because we need to be able to copy an AllocatorDefinition into another
        // so the members are not const, but they are private to avoid accidents.
        version_t version{ kAllocatorVersion };
        lua_Alloc allocF{ nullptr };
        void* allocUD{ nullptr };

        public:

        [[nodiscard]]
        static void* operator new(size_t const size_) noexcept = delete; // can't create one outside of a Lua state
        [[nodiscard]]
        static void* operator new(size_t const size_, lua_State* const L_) noexcept { return lua_newuserdatauv(L_, size_, UserValueCount{ 0 }); }
        // always embedded somewhere else or "in-place constructed" as a full userdata
        // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
        static void operator delete([[maybe_unused]] void* const p_, [[maybe_unused]] lua_State* const L_) {}

        ~AllocatorDefinition() = default;

        AllocatorDefinition(lua_Alloc const allocF_, void* const allocUD_) noexcept
        : allocF{ allocF_ }
        , allocUD{ allocUD_ }
        {
        }

        // rule of 5
        AllocatorDefinition() = default;
        AllocatorDefinition(AllocatorDefinition const& rhs_) = default;
        AllocatorDefinition(AllocatorDefinition&& rhs_) = default;
        AllocatorDefinition& operator=(AllocatorDefinition const& rhs_) = default;
        AllocatorDefinition& operator=(AllocatorDefinition&& rhs_) = default;

        [[nodiscard]]
        static AllocatorDefinition& Validated(lua_State* L_, StackIndex idx_);

        void initFrom(lua_State* const L_)
        {
            allocF = lua_getallocf(L_, &allocUD);
        }

        void installIn(lua_State* const L_) const
        {
            if (allocF) {
                lua_setallocf(L_, allocF, allocUD);
            }
        }

        [[nodiscard]]
        lua_State* newState() const
        {
            return luaW_newstate(allocF, allocUD, luaL_makeseed(nullptr));
        }

        [[nodiscard]]
        void* alloc(size_t const nsize_) const
        {
            return allocF(allocUD, nullptr, 0, nsize_);
        }

        [[nodiscard]]
        void* alloc(void* const ptr_, size_t const osize_, size_t const nsize_) const
        {
            return allocF(allocUD, ptr_, osize_, nsize_);
        }

        void free(void* const ptr_, size_t const osize_) const
        {
            std::ignore = allocF(allocUD, ptr_, osize_, 0);
        }
    };

} // namespace lanes
