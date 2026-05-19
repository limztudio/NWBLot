// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using LogArena = Alloc::GlobalArena;
using LogString = TString<LogArena>;
using LogCharAllocator = LogArena;


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

inline void EnqueueLogMessage(const LogType::Enum type, LogString&& message){
    NWB_FATAL_ASSERT(LoggerDetail::g_logger != nullptr);
    LoggerDetail::g_logger->enqueue(Move(message), type);
}

template<typename... ARGS>
inline void EnqueueFormattedLogMessage(const LogType::Enum type, ARGS&&... args){
    NWB_FATAL_ASSERT(LoggerDetail::g_logger != nullptr);
    auto& logger = *LoggerDetail::g_logger;
    logger.enqueue(StringFormat(logger.arena(), Forward<ARGS>(args)...), type);
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
        logger.enqueue(StringFormat(logger.arena(), __VA_ARGS__), ::NWB::Core::Common::LogType::Type);                         \
    }while(false)

#define NWB_LOGGER_ENQUEUE_MESSAGE_AND_BREAK(Type, BreakMacro, ...)                                                            \
    do{                                                                                                                        \
        NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr);                                               \
        auto& logger = *::NWB::Core::Common::LoggerDetail::g_logger;                                                           \
        logger.enqueue(StringFormat(logger.arena(), __VA_ARGS__), ::NWB::Core::Common::LogType::Type);                         \
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

