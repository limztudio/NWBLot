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

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...){}

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
        ::NWB::Core::Common::LoggerDetail::EnqueueMessage(logger, ::NWB::Core::Common::LogType::Type, __VA_ARGS__);            \
        ::CaptureDiagnosticCrash("logger_" #Type, #Type, __FILE__, __LINE__);                                                  \
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

