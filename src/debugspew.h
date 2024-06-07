#pragma once

#include "lanesconf.h"
#include "universe.h"

// #################################################################################################

#if USE_DEBUG_SPEW()

class DebugSpewIndentScope
{
    private:
    Universe* const U{};

    public:
    static std::string_view const debugspew_indent;

    DebugSpewIndentScope(Universe* U_)
    : U{ U_ }
    {
        if (U) {
            U->debugspewIndentDepth.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ~DebugSpewIndentScope()
    {
        if (U) {
            U->debugspewIndentDepth.fetch_sub(1, std::memory_order_relaxed);
        }
    }
};

// #################################################################################################

inline std::string_view DebugSpewIndent(Universe const* const U_)
{
    return DebugSpewIndentScope::debugspew_indent.substr(0, static_cast<size_t>(U_->debugspewIndentDepth.load(std::memory_order_relaxed)));
}

inline auto& DebugSpew(Universe const* const U_)
{
    if (!U_) {
        return std::cerr;
    }
    return std::cerr << DebugSpewIndent(U_) << " ";
}
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
