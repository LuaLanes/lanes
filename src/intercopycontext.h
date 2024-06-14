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
    std::string_view name; // that one can change when we reuse the context

    private:
    [[nodiscard]] std::string_view findLookupName() const;
    // when mode == LookupMode::FromKeeper, L1 is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
    // whon mode != LookupMode::FromKeeper, L1 is not a keeper state, therefore L1 is the state where we want to raise the error
    lua_State* getErrL() const { return (mode == LookupMode::FromKeeper) ? L2 : L1; }
    [[nodiscard]] LuaType processConversion() const;

    // for use in copyCachedFunction
    void copyFunction() const;
    void lookupNativeFunction() const;

    // for use in inter_copy_function
    void copyCachedFunction() const;
    [[nodiscard]] bool lookupTable() const;

    // for use in inter_copy_table
    void interCopyKeyValuePair() const;
    [[nodiscard]] bool pushCachedMetatable() const;
    [[nodiscard]] bool pushCachedTable() const;

    // for use in inter_copy_userdata
    [[nodiscard]] bool tryCopyClonable() const;
    [[nodiscard]] bool tryCopyDeep() const;

    // copying a single Lua stack item
    [[nodiscard]] bool interCopyBoolean() const;
    [[nodiscard]] bool interCopyFunction() const;
    [[nodiscard]] bool interCopyLightuserdata() const;
    [[nodiscard]] bool interCopyNil() const;
    [[nodiscard]] bool interCopyNumber() const;
    [[nodiscard]] bool interCopyString() const;
    [[nodiscard]] bool interCopyTable() const;
    [[nodiscard]] bool interCopyUserdata() const;

    public:
    [[nodiscard]] InterCopyResult interCopy(int n_) const;
    [[nodiscard]] InterCopyResult interCopyOne() const;
    [[nodiscard]] InterCopyResult interCopyPackage() const;
    [[nodiscard]] InterCopyResult interMove(int n_) const;
};
