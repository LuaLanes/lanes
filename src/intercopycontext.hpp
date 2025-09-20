#pragma once

#include "tools.hpp"
#include "universe.hpp"

// forwards
class Universe;

// #################################################################################################

enum class [[nodiscard]] VT
{
    NORMAL, // keep this one first so that it's the value we get when we default-construct
    KEY,
    METATABLE
};

enum class [[nodiscard]] InterCopyResult
{
    Success,
    NotEnoughValues,
    Error
};

enum class [[nodiscard]] InterCopyOneResult
{
    NotCopied,
    Copied,
    RetryAfterConversion
};

// #################################################################################################

DECLARE_UNIQUE_TYPE(CacheIndex, StackIndex);
DECLARE_UNIQUE_TYPE(SourceIndex, StackIndex);
class InterCopyContext final
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
    [[nodiscard]]
    std::string_view findLookupName() const;
    // when mode == LookupMode::FromKeeper, L1 is a keeper state and L2 is not, therefore L2 is the state where we want to raise the error
    // whon mode != LookupMode::FromKeeper, L1 is not a keeper state, therefore L1 is the state where we want to raise the error
    lua_State* getErrL() const { return (mode == LookupMode::FromKeeper) ? L2.value() : L1.value(); }

    // for use in processConversion
    [[nodiscard]]
    ConvertMode lookupConverter() const;

    // for use in interCopyTable and interCopyUserdata
    [[nodiscard]]
    bool processConversion() const;

    // for use in copyCachedFunction
    void copyFunction() const;
    void lookupNativeFunction() const;

    // for use in inter_copy_function
    void copyCachedFunction() const;
    [[nodiscard]]
    bool lookupTable() const;

    // for use in inter_copy_table
    void interCopyKeyValuePair() const;
    [[nodiscard]]
    bool pushCachedMetatable() const;
    [[nodiscard]]
    bool pushCachedTable() const;

    // for use in inter_copy_userdata
    [[nodiscard]]
    bool lookupUserdata() const;
    [[nodiscard]]
    bool tryCopyClonable() const;
    [[nodiscard]]
    bool tryCopyDeep() const;

    // copying a single Lua stack item
    void interCopyBoolean() const;
    [[nodiscard]]
    bool interCopyFunction() const;
    void interCopyLightuserdata() const;
    [[nodiscard]]
    bool interCopyNil() const;
    void interCopyNumber() const;
    void interCopyString() const;
    [[nodiscard]]
    InterCopyOneResult interCopyTable() const;
    [[nodiscard]]
    InterCopyOneResult interCopyUserdata() const;

    public:
    [[nodiscard]]
    InterCopyResult interCopy(int n_) const;
    [[nodiscard]]
    InterCopyResult interCopyOne() const;
    [[nodiscard]]
    InterCopyResult interCopyPackage() const;
    [[nodiscard]]
    InterCopyResult interMove(int n_) const;
};
