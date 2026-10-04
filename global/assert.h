// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <iostream>
#include <cstdlib>

#include <global/compile.h>
#include <global/diagnostics.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_AssertColon = GLB_TEXT(":");
inline constexpr TStringView s_AssertNewline = GLB_TEXT("\n");
inline constexpr TStringView s_AssertLabel = GLB_TEXT("ASSERT ");
inline constexpr TStringView s_FatalAssertLabel = GLB_TEXT("FATAL ASSERT ");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define GLB_DETAIL_ASSERT_CAPTURE(categoryValue, conditionValue, messageTextValue)       \
    ::CaptureDiagnosticEvent(::DiagnosticEventRecord{                                    \
        .event = ::DiagnosticEventName::s_Assert,                                        \
        .category = categoryValue,                                                       \
        .expression = #conditionValue,                                                   \
        .message = messageTextValue,                                                     \
        .file = __FILE__,                                                                \
        .line = __LINE__,                                                                \
        .terminatesProcess = true,                                                       \
    })

#define GLB_DETAIL_ASSERT_ABORT(label)                                                   \
    GLB_TCERR << label << GLB_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline; \
    ::std::abort()

#define GLB_DETAIL_ASSERT_BODY(categoryValue, label, condition)                          \
{                                                                                        \
    if(!(condition)){                                                                    \
        GLB_DETAIL_ASSERT_CAPTURE(categoryValue, condition, ::StringView{});             \
        GLB_DETAIL_ASSERT_ABORT(label);                                                  \
    }                                                                                    \
}

#define GLB_DETAIL_ASSERT_MSG_BODY(categoryValue, label, condition, ...)                 \
{                                                                                        \
    if(!(condition)){                                                                    \
        const auto diagnosticMessage = ::MakeDiagnosticEventText(__VA_ARGS__);            \
        GLB_DETAIL_ASSERT_CAPTURE(categoryValue, condition, diagnosticMessage.view());  \
        GLB_TCERR << label << GLB_TEXT(__FILE__) << s_AssertColon << __LINE__ << s_AssertNewline << diagnosticMessage.c_str() << s_AssertNewline; \
        ::std::abort();                                                                  \
    }                                                                                    \
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLB_OCCUR_ASSERT
#define GLB_ASSERT(condition) GLB_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition)
#define GLB_ASSERT_MSG(condition, ...) GLB_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_Assert, s_AssertLabel, condition, __VA_ARGS__)
#else
#define GLB_ASSERT(condition)
#define GLB_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if GLB_OCCUR_FATAL_ASSERT
#define GLB_FATAL_ASSERT(condition) GLB_DETAIL_ASSERT_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition)
#define GLB_FATAL_ASSERT_MSG(condition, ...) GLB_DETAIL_ASSERT_MSG_BODY(::DiagnosticEventCategory::s_FatalAssert, s_FatalAssertLabel, condition, __VA_ARGS__)
#else
#define GLB_FATAL_ASSERT(condition)
#define GLB_FATAL_ASSERT_MSG(condition, ...)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

