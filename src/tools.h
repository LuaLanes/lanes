#pragma once

#include "deep.h"
#include "macros_and_utils.h"

// forwards
class Universe;

// #################################################################################################

void push_registry_subtable_mode(lua_State* L, RegistryUniqueKey key_, const char* mode_);
void push_registry_subtable(lua_State* L, RegistryUniqueKey key_);

enum class VT
{
    NORMAL, // keep this one first so that it's the value we get when we default-construct
    KEY,
    METATABLE
};

enum class InterCopyResult
{
    Success,
    NotEnoughValues,
    Error
};

// #################################################################################################

using CacheIndex = Unique<int>;
using SourceIndex = Unique<int>;
class InterCopyContext
{
    public:

    Universe* const U;
    DestState const L2;
    SourceState const L1;
    CacheIndex const L2_cache_i;
    SourceIndex L1_i; // that one can change when we reuse the context
    VT vt; // that one can change when we reuse the context
    LookupMode const mode;
    char const* name; // that one can change when we reuse the context

    private:

    // for use in copy_cached_func
    void copy_func() const;
    void lookup_native_func() const;

    // for use in inter_copy_function
    void copy_cached_func() const;
    [[nodiscard]] bool lookup_table() const;

    // for use in inter_copy_table
    void inter_copy_keyvaluepair() const;
    [[nodiscard]] bool push_cached_metatable() const;

    // for use in inter_copy_userdata
    [[nodiscard]] bool copyclone() const;
    [[nodiscard]] bool copydeep() const;

    // copying a single Lua stack item
    [[nodiscard]] bool inter_copy_boolean() const;
    [[nodiscard]] bool inter_copy_function() const;
    [[nodiscard]] bool inter_copy_lightuserdata() const;
    [[nodiscard]] bool inter_copy_nil() const;
    [[nodiscard]] bool inter_copy_number() const;
    [[nodiscard]] bool inter_copy_string() const;
    [[nodiscard]] bool inter_copy_table() const;
    [[nodiscard]] bool inter_copy_userdata() const;

    public:

    [[nodiscard]] bool inter_copy_one() const;
    [[nodiscard]] InterCopyResult inter_copy_package() const;
    [[nodiscard]] InterCopyResult inter_copy(int n_) const;
    [[nodiscard]] InterCopyResult inter_move(int n_) const;
};

// #################################################################################################

[[nodiscard]] int luaG_nameof(lua_State* L);

void populate_func_lookup_table(lua_State* L, int _i, char const* _name);
void initialize_allocator_function(Universe* U, lua_State* L);

// #################################################################################################

// crc64/we of string "CONFIG_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr RegistryUniqueKey CONFIG_REGKEY{ 0x31CD24894EAE8624ull }; // registry key to access the configuration

// crc64/we of string "LOOKUP_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr RegistryUniqueKey LOOKUP_REGKEY{ 0x5051ED67EE7B51A1ull }; // registry key to access the lookup database
