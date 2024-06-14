#pragma once

#include "uniquekey.h"

class Universe;

enum class LookupMode
{
    LaneBody, // send the lane body directly from the source to the destination lane. keep this one first so that it's the value we get when we default-construct
    ToKeeper, // send a function from a lane to a keeper state
    FromKeeper // send a function from a keeper state to a lane
};

enum class FuncSubType
{
    Bytecode,
    Native,
    FastJIT
};

[[nodiscard]] FuncSubType luaG_getfuncsubtype(lua_State* L_, int i_);

// #################################################################################################

// xxh64 of string "kConfigRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kConfigRegKey{ 0x608379D20A398046ull }; // registry key to access the configuration

// xxh64 of string "kLookupRegKey" generated at https://www.pelock.com/products/hash-calculator
static constexpr RegistryUniqueKey kLookupRegKey{ 0xBF1FC5CF3C6DD47Bull }; // registry key to access the lookup database

// #################################################################################################

namespace tools {
    void PopulateFuncLookupTable(lua_State* const L_, int const i_, std::string_view const& name_);
    [[nodiscard]] std::string_view PushFQN(lua_State* L_, int t_, int last_);
    void SerializeRequire(lua_State* L_);
} // namespace tools
