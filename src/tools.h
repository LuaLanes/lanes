#pragma once

#include "deep.h"
#include "macros_and_utils.h"

// forwards
class Universe;

// ################################################################################################

#ifdef _DEBUG
void luaG_dump(lua_State* L);
#endif // _DEBUG

// ################################################################################################

void push_registry_subtable_mode(lua_State* L, UniqueKey key_, const char* mode_);
void push_registry_subtable(lua_State* L, UniqueKey key_);

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

// ################################################################################################

using CacheIndex = Unique<int>;
using SourceIndex = Unique<int>;
struct InterCopyContext
{

    Universe* const U;
    Dest const L2;
    Source const L1;
    CacheIndex const L2_cache_i;
    SourceIndex L1_i; // that one can change when we reuse the context
    VT vt; // that one can change when we reuse the context
    LookupMode const mode;
    char const* name; // that one can change when we reuse the context

    [[nodiscard]] bool inter_copy_one() const;

    private:

    [[nodiscard]] bool inter_copy_userdata() const;
    [[nodiscard]] bool inter_copy_function() const;
    [[nodiscard]] bool inter_copy_table() const;
    [[nodiscard]] bool copyclone() const;
    [[nodiscard]] bool copydeep() const;
    [[nodiscard]] bool push_cached_metatable() const;
    void copy_func() const;
    void copy_cached_func() const;
    void inter_copy_keyvaluepair() const;

    public:

    [[nodiscard]] InterCopyResult inter_copy_package() const;
    [[nodiscard]] InterCopyResult inter_copy(int n_) const;
    [[nodiscard]] InterCopyResult inter_move(int n_) const;
};

// ################################################################################################

[[nodiscard]] int luaG_nameof(lua_State* L);

void populate_func_lookup_table(lua_State* L, int _i, char const* _name);
void initialize_allocator_function(Universe* U, lua_State* L);

// ################################################################################################

// crc64/we of string "CONFIG_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey CONFIG_REGKEY{ 0x31cd24894eae8624ull }; // registry key to access the configuration

// crc64/we of string "LOOKUP_REGKEY" generated at http://www.nitrxgen.net/hashgen/
static constexpr UniqueKey LOOKUP_REGKEY{ 0x5051ed67ee7b51a1ull }; // registry key to access the lookup database
