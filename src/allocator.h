#pragma once

#include "compat.h"

// #################################################################################################

namespace lanes {

    // everything we need to provide to lua_newstate()
    class AllocatorDefinition
    {
        public:
        // xxh64 of string "kAllocatorVersion_1" generated at https://www.pelock.com/products/hash-calculator
        static constexpr uintptr_t kAllocatorVersion{ static_cast<uintptr_t>(0xCF9D321B0DFB5715ull) };
        uintptr_t version{ kAllocatorVersion };
        lua_Alloc allocF{ nullptr };
        void* allocUD{ nullptr };

        [[nodiscard]] static void* operator new(size_t size_) noexcept = delete; // can't create one outside of a Lua state
        [[nodiscard]] static void* operator new(size_t size_, lua_State* L_) noexcept { return lua_newuserdatauv(L_, size_, 0); }
        // always embedded somewhere else or "in-place constructed" as a full userdata
        // can't actually delete the operator because the compiler generates stack unwinding code that could call it in case of exception
        static void operator delete([[maybe_unused]] void* p_, [[maybe_unused]] lua_State* L_) {}

        AllocatorDefinition(uintptr_t const version_, lua_Alloc const allocF_, void* const allocUD_) noexcept
        : version{ version_ }
        , allocF{ allocF_ }
        , allocUD{ allocUD_ }
        {
        }
        AllocatorDefinition() = default;
        AllocatorDefinition(AllocatorDefinition const& rhs_) = default;
        AllocatorDefinition(AllocatorDefinition&& rhs_) = default;
        AllocatorDefinition& operator=(AllocatorDefinition const& rhs_) = default;
        AllocatorDefinition& operator=(AllocatorDefinition&& rhs_) = default;

        void initFrom(lua_State* L_)
        {
            allocF = lua_getallocf(L_, &allocUD);
        }

        void* alloc(size_t nsize_)
        {
            return allocF(allocUD, nullptr, 0, nsize_);
        }

        void free(void* ptr_, size_t osize_)
        {
            std::ignore = allocF(allocUD, ptr_, osize_, 0);
        }
    };

} // namespace lanes
