// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <format>
#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_ASSERT
#define NWB_ASSERT(condition)                                                                                  \
{                                                                                                              \
    if(!(condition)){                                                                                          \
        NWB_TCERR << NWB_TEXT("ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n"); \
        ::std::abort();                                                                                        \
    }                                                                                                          \
}
#define NWB_ASSERT_MSG(condition, ...)                                                                                                  \
{                                                                                                                                       \
    if(!(condition)){                                                                                                                   \
        const auto msg = ::std::format(__VA_ARGS__);                                                                                    \
        NWB_TCERR << NWB_TEXT("ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n") << msg.c_str() << NWB_TEXT("\n"); \
        ::std::abort();                                                                                                                 \
    }                                                                                                                                   \
}
#else
#define NWB_ASSERT(condition)
#define NWB_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

