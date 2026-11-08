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
    ILogger* previousLogger = nullptr;
    LoggerRegistration* previous = nullptr;
    LoggerRegistration* next = nullptr;
    u64 registerEpoch = 0u;
    u64 unregisterEpoch = 0u;
    LoggerBreakPolicy::Enum breakPolicy = LoggerBreakPolicy::BreakOnErrorAndFatal;
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
        : m_registration{ NotNull(&logger), nullptr, nullptr, nullptr, 0u, 0u, breakPolicy }
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

