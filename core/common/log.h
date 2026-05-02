// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define NWB_LOG_BEGIN NWB_BEGIN namespace Log{
#define NWB_LOG_END }; NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Type{
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
    virtual void enqueue(TString&& str, Type::Enum type = Type::Info) = 0;
    virtual void enqueue(const TString& str, Type::Enum type = Type::Info) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


extern ILogger* g_Logger;

template<typename... ARGS>
constexpr void IgnoreMessage(ARGS&&...){}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class LoggerRegistrationGuard final : NoCopy{
public:
    explicit LoggerRegistrationGuard(ILogger& logger)
        : m_previous(LoggerDetail::g_Logger)
    {
        LoggerDetail::g_Logger = &logger;
    }
    LoggerRegistrationGuard(LoggerRegistrationGuard&&) = delete;
    ~LoggerRegistrationGuard(){
        LoggerDetail::g_Logger = m_previous;
    }


private:
    ILogger* m_previous = nullptr;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_LOG_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if NWB_OCCUR_INFO
#define NWB_LOGGER_INFO(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Info); }
#else
#define NWB_LOGGER_INFO(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ESSENTIAL_INFO
#define NWB_LOGGER_ESSENTIAL_INFO(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::EssentialInfo); }
#else
#define NWB_LOGGER_ESSENTIAL_INFO(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_WARNING
#define NWB_LOGGER_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Warning); }
#else
#define NWB_LOGGER_WARNING(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_CRITICAL_WARNING
#define NWB_LOGGER_CRITICAL_WARNING(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::CriticalWarning); }
#else
#define NWB_LOGGER_CRITICAL_WARNING(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif

#if NWB_OCCUR_ERROR
#define NWB_LOGGER_ERROR(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Error); NWB_SOFTBREAK; }
#define NWB_LOGGER_FATAL(...) { NWB_FATAL_ASSERT(::NWB::Log::LoggerDetail::g_Logger != nullptr); ::NWB::Log::LoggerDetail::g_Logger->enqueue(StringFormat(__VA_ARGS__), ::NWB::Log::Type::Fatal); NWB_HARDBREAK; }
#else
#define NWB_LOGGER_ERROR(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#define NWB_LOGGER_FATAL(...) static_cast<void>(sizeof((::NWB::Log::LoggerDetail::IgnoreMessage(__VA_ARGS__), 0)))
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

