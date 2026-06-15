// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/diagnostics.h>

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using LogArena = Alloc::GlobalArena;
using LogString = TString<LogArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LogType{
    enum Enum : u8{
        Info,
        EssentialInfo,
        Warning,
        CriticalWarning,
        Error,
        Fatal,
    };
};


class ILogger{
public:
    virtual ~ILogger() = default;


public:
    virtual LogArena& arena() = 0;
    virtual void enqueue(LogString&& str, LogType::Enum type = LogType::Info) = 0;
    virtual void enqueue(const LogString& str, LogType::Enum type = LogType::Info) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern ILogger* g_logger;

inline constexpr const char* s_DiagnosticEventCategoryError = "logger_Error";
inline constexpr const char* s_DiagnosticEventCategoryFatal = "logger_Fatal";

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...){}

[[nodiscard]] inline const char* DiagnosticEventNameFromLogType(const LogType::Enum type)noexcept{
    switch(type){
    case LogType::Error:
        return DiagnosticEventName::s_Error;
    case LogType::Fatal:
        return DiagnosticEventName::s_FatalError;
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
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const char* message){
    logger.enqueue(StringConvert(logger.arena(), AStringView(message)), type);
}
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const wchar* message){
    logger.enqueue(StringConvert(logger.arena(), WStringView(message)), type);
}
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const AStringView message){
    logger.enqueue(StringConvert(logger.arena(), message), type);
}
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const WStringView message){
    logger.enqueue(StringConvert(logger.arena(), message), type);
}
template<typename ArenaT>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const TString<ArenaT>& message){
    logger.enqueue(LogString(TStringView(message.data(), message.size()), logger.arena()), type);
}
template<typename In>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, const BasicStringDetail::StringConvertArg<In>& message){
    logger.enqueue(StringConvert(logger.arena(), message.value), type);
}
template<typename... ARGS>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, AFormatString<ARGS...> fmt, ARGS&&... args){
    logger.enqueue(StringFormat(logger.arena(), fmt, Forward<ARGS>(args)...), type);
}
template<typename... ARGS>
inline void EnqueueMessage(ILogger& logger, const LogType::Enum type, WFormatString<ARGS...> fmt, ARGS&&... args){
    logger.enqueue(StringFormat(logger.arena(), fmt, Forward<ARGS>(args)...), type);
}

inline void EnqueuePreparedMessageAndCapture(
    ILogger& logger,
    const LogType::Enum type,
    const char* diagnosticCategory,
    const char* file,
    const u32 line,
    LogString&& message
){
    const DiagnosticEventText diagnosticMessage = MakeDiagnosticEventText(message);
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventNameFromLogType(type),
        .category = diagnosticCategory,
        .message = diagnosticMessage.c_str(),
        .file = file,
        .line = line,
    });
    logger.enqueue(Move(message), type);
}

inline void EnqueuePreparedMessageAndCapture(
    ILogger& logger,
    const LogType::Enum type,
    const char* diagnosticCategory,
    const char* file,
    const u32 line,
    const LogString& message
){
    const DiagnosticEventText diagnosticMessage = MakeDiagnosticEventText(message);
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventNameFromLogType(type),
        .category = diagnosticCategory,
        .message = diagnosticMessage.c_str(),
        .file = file,
        .line = line,
    });
    logger.enqueue(message, type);
}

inline void CaptureMessageDiagnostic(
    const LogType::Enum type,
    const char* diagnosticCategory,
    const char* file,
    const u32 line,
    const DiagnosticEventText& diagnosticMessage
){
    CaptureDiagnosticEvent(DiagnosticEventRecord{
        .event = DiagnosticEventNameFromLogType(type),
        .category = diagnosticCategory,
        .message = diagnosticMessage.c_str(),
        .file = file,
        .line = line,
    });
}

template<typename In>
[[nodiscard]] inline DiagnosticEventText MakeConvertedDiagnosticEventText(const BasicStringDetail::StringConvertArg<In>& message){
    DiagnosticEventText output;
    if constexpr(BasicStringDetail::CanMakeCharView<In>)
        DiagnosticDetail::CopyEventText(output.value, AStringView(message.value));
    else if constexpr(BasicStringDetail::CanMakeWCharView<In>)
        DiagnosticDetail::CopyEventText(output.value, WStringView(message.value));
    return output;
}

inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, LogString&& message){
    EnqueuePreparedMessageAndCapture(logger, type, diagnosticCategory, file, line, Move(message));
}
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const LogString& message){
    EnqueuePreparedMessageAndCapture(logger, type, diagnosticCategory, file, line, message);
}
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const char* message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const wchar* message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const AStringView message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const WStringView message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
template<typename ArenaT>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const TString<ArenaT>& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
template<typename In>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, const BasicStringDetail::StringConvertArg<In>& message){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeConvertedDiagnosticEventText(message));
    EnqueueMessage(logger, type, message);
}
template<typename... ARGS>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, AFormatString<ARGS...> fmt, ARGS&&... args){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText<ARGS...>(fmt, Forward<ARGS>(args)...));
    EnqueueMessage(logger, type, fmt, Forward<ARGS>(args)...);
}
template<typename... ARGS>
inline void EnqueueMessageAndCapture(ILogger& logger, const LogType::Enum type, const char* diagnosticCategory, const char* file, const u32 line, WFormatString<ARGS...> fmt, ARGS&&... args){
    CaptureMessageDiagnostic(type, diagnosticCategory, file, line, MakeDiagnosticEventText<ARGS...>(fmt, Forward<ARGS>(args)...));
    EnqueueMessage(logger, type, fmt, Forward<ARGS>(args)...);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LoggerRegistrationGuard final : NoCopy{
public:
    explicit LoggerRegistrationGuard(ILogger& logger)
        : m_previous(LoggerDetail::g_logger)
    {
        LoggerDetail::g_logger = &logger;
    }
    LoggerRegistrationGuard(LoggerRegistrationGuard&&) = delete;
    ~LoggerRegistrationGuard(){
        LoggerDetail::g_logger = m_previous;
    }


private:
    ILogger* m_previous = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOGGER_IGNORE_MESSAGE(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#define NWB_DIAGNOSTIC_LOGGER_CATEGORY(Type) ::NWB::Core::Common::LoggerDetail::s_DiagnosticEventCategory ## Type

#define NWB_LOGGER_ENQUEUE_MESSAGE(Type, ...)                                                                                  \
    do{                                                                                                                        \
        NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr);                                               \
        auto& logger = *::NWB::Core::Common::LoggerDetail::g_logger;                                                           \
        ::NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, ::NWB::Core::Common::LogType::Type, __VA_ARGS__);            \
    }while(false)

#define NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Type, BreakMacro, ...)                                                            \
    do{                                                                                                                        \
        NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr);                                               \
        auto& logger = *::NWB::Core::Common::LoggerDetail::g_logger;                                                           \
        ::NWB::Core::Common::LoggerDetail::EnqueueMessageAndCapture(                                                           \
            logger,                                                                                                            \
            ::NWB::Core::Common::LogType::Type,                                                                                \
            NWB_DIAGNOSTIC_LOGGER_CATEGORY(Type),                                                                              \
            __FILE__,                                                                                                          \
            __LINE__,                                                                                                          \
            __VA_ARGS__                                                                                                        \
        );                                                                                                                     \
        BreakMacro;                                                                                                            \
    }while(false)


#if NWB_OCCUR_INFO
#define NWB_LOGGER_INFO(...) NWB_LOGGER_ENQUEUE_MESSAGE(Info, __VA_ARGS__)
#else
#define NWB_LOGGER_INFO(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if NWB_OCCUR_ESSENTIAL_INFO
#define NWB_LOGGER_ESSENTIAL_INFO(...) NWB_LOGGER_ENQUEUE_MESSAGE(EssentialInfo, __VA_ARGS__)
#else
#define NWB_LOGGER_ESSENTIAL_INFO(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if NWB_OCCUR_WARNING
#define NWB_LOGGER_WARNING(...) NWB_LOGGER_ENQUEUE_MESSAGE(Warning, __VA_ARGS__)
#else
#define NWB_LOGGER_WARNING(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if NWB_OCCUR_CRITICAL_WARNING
#define NWB_LOGGER_CRITICAL_WARNING(...) NWB_LOGGER_ENQUEUE_MESSAGE(CriticalWarning, __VA_ARGS__)
#else
#define NWB_LOGGER_CRITICAL_WARNING(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif

#if NWB_OCCUR_ERROR
#define NWB_LOGGER_ERROR(...) NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Error, NWB_SOFTBREAK, __VA_ARGS__)
#define NWB_LOGGER_FATAL(...) NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Fatal, NWB_HARDBREAK, __VA_ARGS__)
#else
#define NWB_LOGGER_ERROR(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#define NWB_LOGGER_FATAL(...) NWB_LOGGER_IGNORE_MESSAGE(__VA_ARGS__)
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

