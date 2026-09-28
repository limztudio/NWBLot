// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr tchar s_AssertColon[] = NWB_TEXT(":");
inline constexpr tchar s_AssertNewline[] = NWB_TEXT("\n");
inline constexpr tchar s_AssertLabel[] = NWB_TEXT("ASSERT ");
inline constexpr tchar s_FatalAssertLabel[] = NWB_TEXT("FATAL ASSERT ");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_DETAIL_ASSERT_CAPTURE(categoryValue, conditionValue, messageTextValue)       \
    ::CaptureDiagnosticEvent(::DiagnosticEventRecord{                                    \
        .event = ::DiagnosticEventName::s_Assert.data(),                                 \
        .category = categoryValue.data(),                                                \
        .expression = #conditionValue,                                                   \
        .message = messageTextValue,                                                     \
        .file = __FILE__,                                                                \
        .line = __LINE__,                                                                \
        .terminatesProcess = true,                                                       \
    })

#define NWB_DETAIL_ASSERT_ABORT(label)                                                   \
    NWB_TCERR << label << NWB_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline; \
    ::std::abort()

#define NWB_DETAIL_ASSERT_BODY(categoryValue, label, condition)                          \
{                                                                                        \
    if(!(condition)){                                                                    \
        NWB_DETAIL_ASSERT_CAPTURE(categoryValue, condition, nullptr);                    \
        NWB_DETAIL_ASSERT_ABORT(label);                                                  \
    }                                                                                    \
}

#define NWB_DETAIL_ASSERT_MSG_BODY(categoryValue, label, condition, ...)                 \
{                                                                                        \
    if(!(condition)){                                                                    \
        const auto diagnosticMessage = ::MakeDiagnosticEventText(__VA_ARGS__);            \
        NWB_DETAIL_ASSERT_CAPTURE(categoryValue, condition, diagnosticMessage.c_str());  \
        NWB_TCERR << label << NWB_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline << diagnosticMessage.c_str() << s_AssertNewline; \
        ::std::abort();                                                                  \
    }                                                                                    \
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_ASSERT
#define NWB_ASSERT(condition) NWB_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition)
#define NWB_ASSERT_MSG(condition, ...) NWB_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition, __VA_ARGS__)
#else
#define NWB_ASSERT(condition)
#define NWB_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_FATAL_ASSERT
#define NWB_FATAL_ASSERT(condition) NWB_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition)
#define NWB_FATAL_ASSERT_MSG(condition, ...) NWB_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition, __VA_ARGS__)
#else
#define NWB_FATAL_ASSERT(condition)
#define NWB_FATAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

