#pragma once

#include "deep.h"

// #################################################################################################

class LindaFactory
: public DeepFactory
{
    public:

    // I'm not totally happy with having a 'global' variable. Maybe it should be dynamically created and stored somewhere in the universe?
    static LindaFactory Instance;

    LindaFactory(luaL_Reg const lindaMT_[])
    : mLindaMT{ lindaMT_ }
    {
    }

    private:

    luaL_Reg const* const mLindaMT{ nullptr };

    void createMetatable(lua_State* L_) const override;
    void deleteDeepObjectInternal(lua_State* L_, DeepPrelude* o_) const override;
    char const* moduleName() const override;
    DeepPrelude* newDeepObjectInternal(lua_State* L_) const override;
};
