#pragma once

/*
 * public 'deep' API to be used by external modules if they want to implement Lanes-aware userdata
 * said modules can either link against lanes, or embed compat.cpp/h deep.cpp/h tools.cpp/h universe.cpp/h
 */

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus
#include "lua.h"
#ifdef __cplusplus
}
#endif // __cplusplus

#include "lanesconf.h"
#include "uniquekey.h"

#include <atomic>

// forwards
class Universe;

enum class LookupMode
{
    LaneBody, // send the lane body directly from the source to the destination lane. keep this one first so that it's the value we get when we default-construct
    ToKeeper, // send a function from a lane to a keeper state
    FromKeeper // send a function from a keeper state to a lane
};

// #################################################################################################

// xxh64 of string "kDeepVersion_1" generated at https://www.pelock.com/products/hash-calculator
static constexpr UniqueKey kDeepVersion{ 0x91171AEC6641E9DBull, "kDeepVersion" };

// should be used as header for deep userdata
// a deep userdata is a full userdata that stores a single pointer to the actual DeepPrelude-derived object
struct DeepPrelude
{
    UniqueKey const m_magic{ kDeepVersion };
    // when stored in a keeper state, the full userdata doesn't have a metatable, so we need direct access to the factory
    class DeepFactory& m_factory;
    // data is destroyed when refcount is 0
    std::atomic<int> m_refcount{ 0 };

    DeepPrelude(DeepFactory& factory_)
    : m_factory{ factory_ }
    {
    }
};

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
    virtual DeepPrelude* newDeepObjectInternal(lua_State* L_) const = 0;
    virtual void deleteDeepObjectInternal(lua_State* L_, DeepPrelude* o_) const = 0;
    virtual void createMetatable(lua_State* L_) const = 0;
    virtual char const* moduleName() const = 0;

    public:

    // NVI: public interface
    int pushDeepUserdata(DestState L_, int nuv_) const;
    DeepPrelude* toDeep(lua_State* L_, int index_) const;
    static void DeleteDeepObject(lua_State* L_, DeepPrelude* o_);
    static char const* PushDeepProxy(DestState L_, DeepPrelude* o_, int nuv_, LookupMode mode_);
};
