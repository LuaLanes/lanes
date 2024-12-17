#pragma once

/*
 * public 'deep' API to be used by external modules if they want to implement Lanes-aware userdata
 * said modules can either link against lanes, or embed compat.cpp/h deep.cpp/h tools.cpp/h universe.cpp/h
 */

#include "lanesconf.h"
#include "uniquekey.hpp"

// forwards
enum class LookupMode;
class DeepFactory;
class Universe;

// #################################################################################################

// xxh64 of string "kDeepVersion_1" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kDeepVersion{ 0x91171AEC6641E9DBull, "kDeepVersion" };

// should be used as header for deep userdata
// a deep userdata is a full userdata that stores a single pointer to the actual DeepPrelude-derived object
class DeepPrelude
{
    friend class DeepFactory;

    private:
    UniqueKey const magic{ kDeepVersion };
    // when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the factory
    DeepFactory& factory;

    protected:
    // data is destroyed when refcount is 0
    std::atomic<int> refcount{ 0 };

    protected:
    DeepPrelude(DeepFactory& factory_)
    : factory{ factory_ }
    {
    }

    public:
    void push(lua_State* L_) const;
    [[nodiscard]]
    int getRefcount() const { return refcount.load(std::memory_order_relaxed); }
};

// #################################################################################################

// external C modules should create a single object implementing that interface for each Deep userdata class they want to expose
class DeepFactory
{
    private:
    // the current count of deep object instances
    mutable std::atomic<int> deepObjectCount{};

    protected:
    // protected non-virtual destructor: Lanes won't manage the Factory's lifetime
    DeepFactory() = default;
    virtual ~DeepFactory() = default;

    public:
    // non-copyable, non-movable
    DeepFactory(DeepFactory const&) = delete;
    DeepFactory(DeepFactory const&&) = delete;
    DeepFactory& operator=(DeepFactory const&) = delete;
    DeepFactory& operator=(DeepFactory const&&) = delete;

    private:
    // NVI: private overrides
    virtual void createMetatable(lua_State* L_) const = 0;
    virtual void deleteDeepObjectInternal(lua_State* L_, DeepPrelude* o_) const = 0;
    [[nodiscard]]
    virtual DeepPrelude* newDeepObjectInternal(lua_State* L_) const = 0;
    [[nodiscard]]
    virtual std::string_view moduleName() const = 0;

    private:
    [[nodiscard]]
    static int DeepGC(lua_State* L_);
    void storeDeepLookup(lua_State* L_) const;

    public:
    // NVI: public interface
    static void DeleteDeepObject(lua_State* L_, DeepPrelude* o_);
    [[nodiscard]]
    int getObjectCount() const { return deepObjectCount.load(std::memory_order_relaxed); }
    [[nodiscard]]
    static bool IsDeepUserdata(lua_State* L_, StackIndex idx_);
    [[nodiscard]]
    static DeepFactory* LookupFactory(lua_State* L_, StackIndex index_, LookupMode mode_);
    static void PushDeepProxy(DestState L_, DeepPrelude* o_, UserValueCount nuv_, LookupMode mode_, lua_State* errL_);
    void pushDeepUserdata(DestState L_, UserValueCount nuv_) const;
    [[nodiscard]]
    DeepPrelude* toDeep(lua_State* L_, StackIndex index_) const;
};

// #################################################################################################
