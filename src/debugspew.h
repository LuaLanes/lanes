#pragma once

#include "lanesconf.h"
#include "universe.h"

// #################################################################################################

#if USE_DEBUG_SPEW()

class DebugSpewIndentScope
{
    private:
    Universe* const U;

    public:
    static char const* const debugspew_indent;

    DebugSpewIndentScope(Universe* U_)
    : U{ U_ }
    {
        if (U)
            U->debugspewIndentDepth.fetch_add(1, std::memory_order_relaxed);
    }

    ~DebugSpewIndentScope()
    {
        if (U)
            U->debugspewIndentDepth.fetch_sub(1, std::memory_order_relaxed);
    }
};

// #################################################################################################

#define INDENT_BEGIN "%.*s "
#define INDENT_END(U_) , (U_ ? U_->debugspewIndentDepth.load(std::memory_order_relaxed) : 0), DebugSpewIndentScope::debugspew_indent
#define DEBUGSPEW_CODE(_code) _code
#define DEBUGSPEW_OR_NOT(a_, b_) a_
#define DEBUGSPEW_PARAM_COMMA(param_) param_,
#define DEBUGSPEW_COMMA_PARAM(param_) , param_

#else // USE_DEBUG_SPEW()

#define DEBUGSPEW_CODE(_code)
#define DEBUGSPEW_OR_NOT(a_, b_) b_
#define DEBUGSPEW_PARAM_COMMA(param_)
#define DEBUGSPEW_COMMA_PARAM(param_)

#endif // USE_DEBUG_SPEW()
