// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


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
    virtual void enqueue(TString&& str, LogType::Enum type = LogType::Info) = 0;
    virtual void enqueue(const TString& str, LogType::Enum type = LogType::Info) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern ILogger* g_logger;

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...){}


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


#if NWB_OCCUR_INFO
#define NWB_LOGGER_INFO(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::Info); }
#else
#define NWB_LOGGER_INFO(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ESSENTIAL_INFO
#define NWB_LOGGER_ESSENTIAL_INFO(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::EssentialInfo); }
#else
#define NWB_LOGGER_ESSENTIAL_INFO(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_WARNING
#define NWB_LOGGER_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::Warning); }
#else
#define NWB_LOGGER_WARNING(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_CRITICAL_WARNING
#define NWB_LOGGER_CRITICAL_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::CriticalWarning); }
#else
#define NWB_LOGGER_CRITICAL_WARNING(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ERROR
#define NWB_LOGGER_ERROR(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::Error); NWB_SOFTBREAK; }
#define NWB_LOGGER_FATAL(...) { NWB_FATAL_ASSERT(::NWB::Core::Common::LoggerDetail::g_logger != nullptr); ::NWB::Core::Common::LoggerDetail::g_logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Core::Common::LogType::Fatal); NWB_HARDBREAK; }
#else
#define NWB_LOGGER_ERROR(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#define NWB_LOGGER_FATAL(...) static_cast<void>(sizeof((::NWB::Core::Common::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

