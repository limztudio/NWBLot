// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <format>
#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_ASSERT
#define NWB_ASSERT(condition)                                                                                  \
{                                                                                                              \
    if(!(condition)){                                                                                          \
        ::CaptureDiagnosticCrash("assert", #condition, __FILE__, __LINE__);                                    \
        NWB_TCERR << NWB_TEXT("ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n"); \
        ::std::abort();                                                                                        \
    }                                                                                                          \
}
#define NWB_ASSERT_MSG(condition, ...)                                                                                                          \
{                                                                                                                                               \
    if(!(condition)){                                                                                                                           \
        const auto msg = ::std::format(__VA_ARGS__);                                                                                            \
        ::CaptureDiagnosticCrash("assert", #condition, __FILE__, __LINE__);                                                                      \
        NWB_TCERR << NWB_TEXT("ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n") << msg.c_str() << NWB_TEXT("\n"); \
        ::std::abort();                                                                                                                         \
    }                                                                                                                                           \
}
#else
#define NWB_ASSERT(condition)
#define NWB_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_FATAL_ASSERT
#define NWB_FATAL_ASSERT(condition)                                                                                  \
{                                                                                                                    \
    if(!(condition)){                                                                                                \
        ::CaptureDiagnosticCrash("fatal_assert", #condition, __FILE__, __LINE__);                                    \
        NWB_TCERR << NWB_TEXT("FATAL ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n"); \
        ::std::abort();                                                                                              \
    }                                                                                                                \
}
#define NWB_FATAL_ASSERT_MSG(condition, ...)                                                                                                          \
{                                                                                                                                                     \
    if(!(condition)){                                                                                                                                 \
        const auto msg = ::std::format(__VA_ARGS__);                                                                                                  \
        ::CaptureDiagnosticCrash("fatal_assert", #condition, __FILE__, __LINE__);                                                                      \
        NWB_TCERR << NWB_TEXT("FATAL ASSERT ") << NWB_TEXT(__FILE__) << NWB_TEXT(":") << __LINE__ << NWB_TEXT("\n") << msg.c_str() << NWB_TEXT("\n"); \
        ::std::abort();                                                                                                                               \
    }                                                                                                                                                 \
}
#else
#define NWB_FATAL_ASSERT(condition)
#define NWB_FATAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

