// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "logger_registration.h"

#include <global/diagnostics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_DiagnosticEventCategoryError = "logger_Error";
inline constexpr StringView s_DiagnosticEventCategoryFatal = "logger_Fatal";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline constexpr b8 ShouldBreak(const LoggerBreakPolicy::Enum breakPolicy, const LogType::Enum type)noexcept{
    switch(breakPolicy){
    case LoggerBreakPolicy::ReportOnly:
        return false;
    case LoggerBreakPolicy::BreakOnFatal:
        return type == LogType::Fatal;
    case LoggerBreakPolicy::BreakOnErrorAndFatal:
        return type == LogType::Error || type == LogType::Fatal;
    }
    return type == LogType::Error || type == LogType::Fatal;
}

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...)noexcept{}

[[nodiscard]] constexpr StringView DiagnosticEventNameFromLogType(const LogType::Enum type)noexcept{
    switch(type){
    case LogType::Assert:
        return DiagnosticEventName::s_Assert;
    case LogType::Error:
        return DiagnosticEventName::s_Error;
    case LogType::Fatal:
        return DiagnosticEventName::s_Fatal;
    default:
        return "";
    }
}

inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, LogString&& message){
    logger.enqueue(Move(message), type);
}

inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const LogString& message){
    logger.enqueue(message, type);
}

inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const AStringView message){
    logger.enqueue(ConvertString(logger.arena(), message), type);
}

inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const WStringView message){
    logger.enqueue(ConvertString(logger.arena(), message), type);
}

template<typename In>
    requires((IsPointer_V<In> || IsArray_V<In>) && (CanMakeCharView<In> || CanMakeWCharView<In>))
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const In& message){
    if constexpr(CanMakeCharView<In>)
        EnqueueMessage(logger, type, AStringView(message));
    else
        EnqueueMessage(logger, type, WStringView(message));
}

template<typename ArenaT>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const TString<ArenaT>& message){
    logger.enqueue(LogString(TStringView(message.data(), message.size()), logger.arena()), type);
}

template<typename In>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const ConvertStringArg<In>& message){
    logger.enqueue(ConvertString(logger.arena(), message.value), type);
}

template<typename... ARGS>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, AFormatString<ARGS...> fmt, ARGS&&... args){
    logger.enqueue(StringFormat(logger.arena(), fmt, Forward<ARGS>(args)...), type);
}

template<typename... ARGS>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, WFormatString<ARGS...> fmt, ARGS&&... args){
    logger.enqueue(StringFormat(logger.arena(), fmt, Forward<ARGS>(args)...), type);
}

inline void CaptureMessageDiagnostic(const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const DiagnosticEventText& diagnosticMessage)noexcept{
    CaptureDiagnosticEvent(DiagnosticEventRecord{ .event = DiagnosticEventNameFromLogType(type), .category = diagnosticCategory, .message = diagnosticMessage.view(), .file = file, .line = line, });
}

template<typename In>
[[nodiscard]] inline DiagnosticEventText MakeConvertedDiagnosticEventText(const ConvertStringArg<In>& message)noexcept([]()consteval noexcept{
    if constexpr(CanMakeCharView<In>)
        return noexcept(AStringView(DeclVal<const In&>()));
    else if constexpr(CanMakeWCharView<In>)
        return noexcept(WStringView(DeclVal<const In&>()));
    else
        return true;
}()){
    DiagnosticEventText output;
    if constexpr(CanMakeCharView<In>)
        CopyEventText(output.value, AStringView(message.value));
    else if constexpr(CanMakeWCharView<In>)
        CopyEventText(output.value, WStringView(message.value));
    return output;
}

inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, LogString&& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    logger.enqueue(Move(message), type);
}

inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const LogString& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    logger.enqueue(message, type);
}

inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const AStringView message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}

inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const WStringView message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}

template<typename In>
    requires((IsPointer_V<In> || IsArray_V<In>) && (CanMakeCharView<In> || CanMakeWCharView<In>))
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const In& message){
    if constexpr(CanMakeCharView<In>)
        EnqueueMessageAndCapture(logger, type, diagnosticCategory, file, line, AStringView(message));
    else
        EnqueueMessageAndCapture(logger, type, diagnosticCategory, file, line, WStringView(message));
}

template<typename ArenaT>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const TString<ArenaT>& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}

template<typename In>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, const ConvertStringArg<In>& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeConvertedDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}

template<typename... ARGS>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, AFormatString<ARGS...> fmt, ARGS&&... args){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText<ARGS...>(fmt, Forward<ARGS>(args)...));
    EnqueueMessage(logger, type, fmt, Forward<ARGS>(args)...);
}

template<typename... ARGS>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const StringView diagnosticCategory, const StringView file, const u32 line, WFormatString<ARGS...> fmt, ARGS&&... args){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText<ARGS...>(fmt, Forward<ARGS>(args)...));
    EnqueueMessage(logger, type, fmt, Forward<ARGS>(args)...);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOGGER_IGNORE_MESSAGE(...)                                                                                         \
    do{                                                                                                                        \
        (void)sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0));                                      \
    }while(false)
#define NWB_DIAGNOSTIC_LOGGER_CATEGORY(Type) ::NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategory ## Type

#define NWB_LOGGER_ENQUEUE_MESSAGE(Type, ...)                                                                                  \
    do{                                                                                                                        \
        ::NWB::Core::Common::LoggerDetail::LoggerRegistrationLease loggerLease;                                                \
        FOUNDATION_FATAL_ASSERT(loggerLease.valid());                                                                                 \
        auto& logger = loggerLease.logger();                                                                                   \
        ::NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, ::NWB::Core::Common::LogType::Type, __VA_ARGS__);            \
    }while(false)

#define NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Type, BreakMacro, ...)                                                            \
    do{                                                                                                                        \
        ::NWB::Core::Common::LoggerDetail::LoggerRegistrationLease loggerLease;                                                \
        FOUNDATION_FATAL_ASSERT(loggerLease.valid());                                                                                 \
        auto& logger = loggerLease.logger();                                                                                   \
        ::NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(                                                           \
            logger,                                                                                                            \
            ::NWB::Core::Common::LogType::Type,                                                                                \
            NWB_DIAGNOSTIC_LOGGER_CATEGORY(Type),                                                                              \
            __FILE__,                                                                                                          \
            __LINE__,                                                                                                          \
            __VA_ARGS__                                                                                                        \
        );                                                                                                                     \
        if(::NWB::Core::Common::LoggerDetail::ShouldBreak(                                                                     \
            loggerLease.breakPolicy(),                                                                                         \
            ::NWB::Core::Common::LogType::Type                                                                                 \
        )){                                                                                                                    \
            BreakMacro;                                                                                                        \
        }                                                                                                                      \
    }while(false)

#if FOUNDATION_OCCUR_INFO
#define NWB_LOGGER_INFO(...) NWB_LOGGER_ENQUEUE_MESSAGE(Info, __VA_ARGS__)
#else
#define NWB_LOGGER_INFO(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if FOUNDATION_OCCUR_ESSENTIAL_INFO
#define NWB_LOGGER_ESSENTIAL_INFO(...) NWB_LOGGER_ENQUEUE_MESSAGE(EssentialInfo, __VA_ARGS__)
#else
#define NWB_LOGGER_ESSENTIAL_INFO(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if FOUNDATION_OCCUR_WARNING
#define NWB_LOGGER_WARNING(...) NWB_LOGGER_ENQUEUE_MESSAGE(Warning, __VA_ARGS__)
#else
#define NWB_LOGGER_WARNING(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if FOUNDATION_OCCUR_CRITICAL_WARNING
#define NWB_LOGGER_CRITICAL_WARNING(...) NWB_LOGGER_ENQUEUE_MESSAGE(CriticalWarning, __VA_ARGS__)
#else
#define NWB_LOGGER_CRITICAL_WARNING(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if FOUNDATION_OCCUR_ERROR
#define NWB_LOGGER_ERROR(...) NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Error, FOUNDATION_SOFTBREAK, __VA_ARGS__)
#define NWB_LOGGER_FATAL(...) NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Fatal, FOUNDATION_HARDBREAK, __VA_ARGS__)
#else
#define NWB_LOGGER_ERROR(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#define NWB_LOGGER_FATAL(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

