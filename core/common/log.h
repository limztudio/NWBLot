// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/general.h>

#include <global/diagnostics.h>


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
        Assert,
    };
};

namespace LoggerBreakPolicy{
    enum Enum : u8{
        ReportOnly,
        BreakOnFatal,
        BreakOnErrorAndFatal,
    };
};

class ILogger{
public:
    virtual ~ILogger()noexcept = default;


public:
    virtual LogArena& arena() = 0;
    virtual void enqueue(LogString&& str, LogType::Enum type = LogType::Info) = 0;
    virtual void enqueue(const LogString& str, LogType::Enum type = LogType::Info) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using LoggerRegistrationCallback = void(*)(void* context, ILogger* previousLogger)noexcept;

struct LoggerRegistration{
    NotNull<ILogger*> logger;
    LoggerBreakPolicy::Enum breakPolicy = LoggerBreakPolicy::BreakOnErrorAndFatal;
    ILogger* previousLogger = nullptr;
    LoggerRegistration* previous = nullptr;
    LoggerRegistration* next = nullptr;
    u64 registerEpoch = 0u;
    u64 unregisterEpoch = 0u;
    b8 registered = false;
};

class LoggerRegistrationLease final : NoCopy{
public:
    LoggerRegistrationLease()noexcept;
    LoggerRegistrationLease(LoggerRegistrationLease&&) = delete;
    ~LoggerRegistrationLease()noexcept;


public:
    [[nodiscard]] b8 valid()const noexcept{ return m_logger != nullptr; }

    [[nodiscard]] ILogger& logger()const noexcept{ return *m_logger; }

    [[nodiscard]] LoggerBreakPolicy::Enum breakPolicy()const noexcept{ return m_breakPolicy; }


private:
    friend void RegisterLogger(
        LoggerRegistration& registration,
        ILogger& logger,
        const LoggerBreakPolicy::Enum* breakPolicyOverride,
        LoggerRegistrationCallback beforePublish,
        void* callbackContext
    )noexcept;
    friend void UnregisterLogger(
        LoggerRegistration& registration,
        LoggerRegistrationCallback afterUnpublish,
        void* callbackContext
    )noexcept;


private:
    ILogger* m_logger = nullptr;
    LoggerBreakPolicy::Enum m_breakPolicy = LoggerBreakPolicy::BreakOnErrorAndFatal;
    u64 m_acquireEpoch = 0u;
    const void* m_threadTag = nullptr;
    LoggerRegistrationLease* m_previous = nullptr;
    LoggerRegistrationLease* m_next = nullptr;
};

void RegisterLogger(
    LoggerRegistration& registration,
    ILogger& logger,
    const LoggerBreakPolicy::Enum* breakPolicyOverride = nullptr,
    LoggerRegistrationCallback beforePublish = nullptr,
    void* callbackContext = nullptr
)noexcept;
void UnregisterLogger(
    LoggerRegistration& registration,
    LoggerRegistrationCallback afterUnpublish = nullptr,
    void* callbackContext = nullptr
)noexcept;

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


class LoggerRegistrationGuard final : NoCopy{
public:
    explicit LoggerRegistrationGuard(ILogger& logger)noexcept
        : m_registration{ NotNull(&logger) }
    {
        LoggerDetail::RegisterLogger(m_registration, logger);
    }
    LoggerRegistrationGuard(ILogger& logger, const LoggerBreakPolicy::Enum breakPolicy)noexcept
        : m_registration{ NotNull(&logger), breakPolicy }
    {
        LoggerDetail::RegisterLogger(m_registration, logger, &breakPolicy);
    }
    LoggerRegistrationGuard(
        ILogger& logger,
        const LoggerDetail::LoggerRegistrationCallback beforePublish,
        const LoggerDetail::LoggerRegistrationCallback afterUnpublish,
        void* const callbackContext
    )noexcept
        : m_registration{ NotNull(&logger) }
        , m_afterUnpublish(afterUnpublish)
        , m_callbackContext(callbackContext)
    {
        LoggerDetail::RegisterLogger(m_registration, logger, nullptr, beforePublish, callbackContext);
    }
    LoggerRegistrationGuard(LoggerRegistrationGuard&&) = delete;
    ~LoggerRegistrationGuard()noexcept{
        LoggerDetail::UnregisterLogger(m_registration, m_afterUnpublish, m_callbackContext);
    }


public:
    [[nodiscard]] ILogger* previousLogger()const noexcept{ return m_registration.previousLogger; }


private:
    LoggerDetail::LoggerRegistration m_registration;
    LoggerDetail::LoggerRegistrationCallback m_afterUnpublish = nullptr;
    void* m_callbackContext = nullptr;
};


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
        NWB_FATAL_ASSERT(loggerLease.valid());                                                                                 \
        auto& logger = loggerLease.logger();                                                                                   \
        ::NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, ::NWB::Core::Common::LogType::Type, __VA_ARGS__);            \
    }while(false)

#define NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Type, BreakMacro, ...)                                                            \
    do{                                                                                                                        \
        ::NWB::Core::Common::LoggerDetail::LoggerRegistrationLease loggerLease;                                                \
        NWB_FATAL_ASSERT(loggerLease.valid());                                                                                 \
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

