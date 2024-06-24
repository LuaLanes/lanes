#pragma once

/*
 * public 'deep' API to be used by external modules if they want to implement Lanes-aware userdata
 * said modules can either link against lanes, or embed compat.cpp/h deep.cpp/h tools.cpp/h universe.cpp/h
 */

#include "lanesconf.h"
#include "uniquekey.h"

// forwards
enum class LookupMode;
class DeepFactory;
class Universe;

// #################################################################################################

// xxh64 of string "kDeepVersion_1" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kDeepVersion{ 0x91171AEC6641E9DBull, "kDeepVersion" };

// should be used as header for deep userdata
// a deep userdata is a full userdata that stores a single pointer to the actual DeepPrelude-derived object
struct DeepPrelude
{
    UniqueKey const magic{ kDeepVersion };
    // when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the factory
    DeepFactory& factory;
    // data is destroyed when refcount is 0
    std::atomic<int> refcount{ 0 };

    DeepPrelude(DeepFactory& factory_)
    : factory{ factory_ }
    {
    }

    void push(lua_State* L_) const;
};

// #################################################################################################

// external C modules should create a single object implementing that interface for each Deep userdata class they want to expose
class DeepFactory
{
    protected:
    // protected non-virtual destructor: Lanes won't manage the Factory's lifetime
    DeepFactory() = default;
    ~DeepFactory() = default;

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
    [[nodiscard]] virtual DeepPrelude* newDeepObjectInternal(lua_State* L_) const = 0;
    [[nodiscard]] virtual std::string_view moduleName() const = 0;

    private:
    void storeDeepLookup(lua_State* L_) const;

    public:
    // NVI: public interface
    static void DeleteDeepObject(lua_State* L_, DeepPrelude* o_);
    [[nodiscard]] static bool IsDeepUserdata(lua_State* const L_, int const idx_);
    [[nodiscard]] static DeepFactory* LookupFactory(lua_State* L_, int index_, LookupMode mode_);
    static void PushDeepProxy(DestState L_, DeepPrelude* o_, int nuv_, LookupMode mode_, lua_State* errL_);
    void pushDeepUserdata(DestState L_, int nuv_) const;
    [[nodiscard]] DeepPrelude* toDeep(lua_State* L_, int index_) const;
};

// #################################################################################################
