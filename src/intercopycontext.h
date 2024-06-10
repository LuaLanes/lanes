#pragma once

#include "tools.h"

// forwards
class Universe;

// #################################################################################################

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
    [[nodiscard]] std::string_view findLookupName() const;
    // when mode == LookupMode::FromKeeper, L1 is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
    // whon mode != LookupMode::FromKeeper, L1 is not a keeper state, therefore L1 is the state where we want to raise the error
    lua_State* getErrL() const { return (mode == LookupMode::FromKeeper) ? L2 : L1; }
    [[nodiscard]] LuaType processConversion() const;

    // for use in copy_cached_func
    void copy_func() const;
    void lookup_native_func() const;

    // for use in inter_copy_function
    void copy_cached_func() const;
    [[nodiscard]] bool lookup_table() const;

    // for use in inter_copy_table
    void inter_copy_keyvaluepair() const;
    [[nodiscard]] bool push_cached_metatable() const;
    [[nodiscard]] bool push_cached_table() const;

    // for use in inter_copy_userdata
    [[nodiscard]] bool tryCopyClonable() const;
    [[nodiscard]] bool tryCopyDeep() const;

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
