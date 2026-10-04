// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_AssertColon = GLOBAL_TEXT(":");
inline constexpr TStringView s_AssertNewline = GLOBAL_TEXT("\n");
inline constexpr TStringView s_AssertLabel = GLOBAL_TEXT("ASSERT ");
inline constexpr TStringView s_FatalAssertLabel = GLOBAL_TEXT("FATAL ASSERT ");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define GLOBAL_DETAIL_ASSERT_CAPTURE(categoryValue, conditionValue, messageTextValue)       \
    ::CaptureDiagnosticEvent(::DiagnosticEventRecord{                                    \
        .event = ::DiagnosticEventName::s_Assert,                                        \
        .category = categoryValue,                                                       \
        .expression = #conditionValue,                                                   \
        .message = messageTextValue,                                                     \
        .file = __FILE__,                                                                \
        .line = __LINE__,                                                                \
        .terminatesProcess = true,                                                       \
    })

#define GLOBAL_DETAIL_ASSERT_ABORT(label)                                                   \
    GLOBAL_TCERR << label << GLOBAL_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline; \
    ::std::abort()

#define GLOBAL_DETAIL_ASSERT_BODY(categoryValue, label, condition)                          \
{                                                                                        \
    if(!(condition)){                                                                    \
        GLOBAL_DETAIL_ASSERT_CAPTURE(categoryValue, condition, ::StringView{});             \
        GLOBAL_DETAIL_ASSERT_ABORT(label);                                                  \
    }                                                                                    \
}

#define GLOBAL_DETAIL_ASSERT_MSG_BODY(categoryValue, label, condition, ...)                 \
{                                                                                        \
    if(!(condition)){                                                                    \
        const auto diagnosticMessage = ::MakeDiagnosticEventText(__VA_ARGS__);            \
        GLOBAL_DETAIL_ASSERT_CAPTURE(categoryValue, condition, diagnosticMessage.view());  \
        GLOBAL_TCERR << label << GLOBAL_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline << diagnosticMessage.c_str() << s_AssertNewline; \
        ::std::abort();                                                                  \
    }                                                                                    \
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLOBAL_OCCUR_ASSERT
#define GLOBAL_ASSERT(condition) GLOBAL_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition)
#define GLOBAL_ASSERT_MSG(condition, ...) GLOBAL_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition, __VA_ARGS__)
#else
#define GLOBAL_ASSERT(condition)
#define GLOBAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLOBAL_OCCUR_FATAL_ASSERT
#define GLOBAL_FATAL_ASSERT(condition) GLOBAL_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition)
#define GLOBAL_FATAL_ASSERT_MSG(condition, ...) GLOBAL_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition, __VA_ARGS__)
#else
#define GLOBAL_FATAL_ASSERT(condition)
#define GLOBAL_FATAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

