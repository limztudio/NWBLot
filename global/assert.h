// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_AssertColon = FOUNDATION_TEXT(":");
inline constexpr TStringView s_AssertNewline = FOUNDATION_TEXT("\n");
inline constexpr TStringView s_AssertLabel = FOUNDATION_TEXT("ASSERT ");
inline constexpr TStringView s_FatalAssertLabel = FOUNDATION_TEXT("FATAL ASSERT ");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define FOUNDATION_DETAIL_ASSERT_CAPTURE(categoryValue, conditionValue, messageTextValue)       \
    ::CaptureDiagnosticEvent(::DiagnosticEventRecord{                                    \
        .event = ::DiagnosticEventName::s_Assert,                                        \
        .category = categoryValue,                                                       \
        .expression = #conditionValue,                                                   \
        .message = messageTextValue,                                                     \
        .file = __FILE__,                                                                \
        .line = __LINE__,                                                                \
        .terminatesProcess = true,                                                       \
    })

#define FOUNDATION_DETAIL_ASSERT_ABORT(label)                                                   \
    FOUNDATION_TCERR << label << FOUNDATION_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline; \
    ::std::abort()

#define FOUNDATION_DETAIL_ASSERT_BODY(categoryValue, label, condition)                          \
{                                                                                        \
    if(!(condition)){                                                                    \
        FOUNDATION_DETAIL_ASSERT_CAPTURE(categoryValue, condition, ::StringView{});             \
        FOUNDATION_DETAIL_ASSERT_ABORT(label);                                                  \
    }                                                                                    \
}

#define FOUNDATION_DETAIL_ASSERT_MSG_BODY(categoryValue, label, condition, ...)                 \
{                                                                                        \
    if(!(condition)){                                                                    \
        const auto diagnosticMessage = ::MakeDiagnosticEventText(__VA_ARGS__);            \
        FOUNDATION_DETAIL_ASSERT_CAPTURE(categoryValue, condition, diagnosticMessage.view());  \
        FOUNDATION_TCERR << label << FOUNDATION_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline << diagnosticMessage.c_str() << s_AssertNewline; \
        ::std::abort();                                                                  \
    }                                                                                    \
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if FOUNDATION_OCCUR_ASSERT
#define FOUNDATION_ASSERT(condition) FOUNDATION_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition)
#define FOUNDATION_ASSERT_MSG(condition, ...) FOUNDATION_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition, __VA_ARGS__)
#else
#define FOUNDATION_ASSERT(condition)
#define FOUNDATION_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if FOUNDATION_OCCUR_FATAL_ASSERT
#define FOUNDATION_FATAL_ASSERT(condition) FOUNDATION_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition)
#define FOUNDATION_FATAL_ASSERT_MSG(condition, ...) FOUNDATION_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition, __VA_ARGS__)
#else
#define FOUNDATION_FATAL_ASSERT(condition)
#define FOUNDATION_FATAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

