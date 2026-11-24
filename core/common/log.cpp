// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "logger_registration.h"

#include <global/sync.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace LoggerDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common_log{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Futex s_RegistrationMutex;
static LoggerRegistration* s_LoggerRegistration = nullptr;
static LoggerRegistrationLease* s_ActiveLeasesHead = nullptr;
static u64 s_RegistrationEpoch = 0u;
static usize s_WaitingUnregistrations = 0u;
static Atomic<u32> s_LeaseReleaseSequence{ 0u };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static const void* CurrentThreadTag()noexcept{
    thread_local u8 s_ThreadTag = 0u;
    return &s_ThreadTag;
}

static void UnlinkRegistration(LoggerRegistration& registration)noexcept{
    if(!registration.registered)
        return;

    if(registration.previous)
        registration.previous->next = registration.next;
    if(registration.next)
        registration.next->previous = registration.previous;
    else if(s_LoggerRegistration == &registration)
        s_LoggerRegistration = registration.previous;

    registration.previous = nullptr;
    registration.next = nullptr;
    registration.unregisterEpoch = ++s_RegistrationEpoch;
    registration.registered = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LoggerRegistrationLease::LoggerRegistrationLease()noexcept{
    NothrowScopedLock lock(__hidden_common_log::s_RegistrationMutex);
    LoggerRegistration* const active = __hidden_common_log::s_LoggerRegistration;
    if(!active)
        return;

    m_logger = active->logger.get();
    m_breakPolicy = active->breakPolicy;
    m_acquireEpoch = __hidden_common_log::s_RegistrationEpoch;
    m_threadTag = __hidden_common_log::CurrentThreadTag();
    m_previous = nullptr;
    m_next = __hidden_common_log::s_ActiveLeasesHead;
    if(m_next)
        m_next->m_previous = this;
    __hidden_common_log::s_ActiveLeasesHead = this;
}

LoggerRegistrationLease::~LoggerRegistrationLease()noexcept{
    if(!m_logger)
        return;

    b8 notifyWaiters = false;
    {
        NothrowScopedLock lock(__hidden_common_log::s_RegistrationMutex);
        if(m_previous)
            m_previous->m_next = m_next;
        else if(__hidden_common_log::s_ActiveLeasesHead == this)
            __hidden_common_log::s_ActiveLeasesHead = m_next;
        if(m_next)
            m_next->m_previous = m_previous;
        m_previous = nullptr;
        m_next = nullptr;

        if(__hidden_common_log::s_WaitingUnregistrations > 0u){
            __hidden_common_log::s_LeaseReleaseSequence.fetch_add(1u, MemoryOrder::release);
            notifyWaiters = true;
        }
    }
    if(notifyWaiters)
        __hidden_common_log::s_LeaseReleaseSequence.notify_all();
}

void RegisterLogger(
    LoggerRegistration& registration,
    ILogger& logger,
    const LoggerBreakPolicy::Enum* const breakPolicyOverride,
    const LoggerRegistrationCallback beforePublish,
    void* const callbackContext
)noexcept{
    NothrowScopedLock lock(__hidden_common_log::s_RegistrationMutex);
    LoggerRegistration* const previous = __hidden_common_log::s_LoggerRegistration;
    ILogger* const previousLogger = previous ? previous->logger.get() : nullptr;

    registration.logger = NotNull(&logger);
    registration.breakPolicy = breakPolicyOverride
        ? *breakPolicyOverride
        : (previous ? previous->breakPolicy : LoggerBreakPolicy::BreakOnErrorAndFatal)
    ;
    registration.previousLogger = previousLogger;
    if(beforePublish)
        beforePublish(callbackContext, previousLogger);

    registration.previous = previous;
    registration.next = nullptr;
    if(previous)
        previous->next = &registration;
    registration.registerEpoch = ++__hidden_common_log::s_RegistrationEpoch;
    registration.unregisterEpoch = Limit<u64>::s_Max;
    registration.registered = true;
    __hidden_common_log::s_LoggerRegistration = &registration;
}

void UnregisterLogger(
    LoggerRegistration& registration,
    const LoggerRegistrationCallback afterUnpublish,
    void* const callbackContext
)noexcept{
    const void* const currentThreadTag = __hidden_common_log::CurrentThreadTag();
    __hidden_common_log::s_RegistrationMutex.lock();
    __hidden_common_log::UnlinkRegistration(registration);

    const auto hasActiveExternalLease = [&registration, currentThreadTag]()noexcept{
        for(
            const LoggerRegistrationLease* lease = __hidden_common_log::s_ActiveLeasesHead;
            lease != nullptr;
            lease = lease->m_next
        ){
            if(
                lease->m_threadTag != currentThreadTag
                && lease->m_acquireEpoch >= registration.registerEpoch
                && lease->m_acquireEpoch < registration.unregisterEpoch
            )
                return true;
        }
        return false;
    };

    ++__hidden_common_log::s_WaitingUnregistrations;
    while(hasActiveExternalLease()){
        const u32 observedSequence = __hidden_common_log::s_LeaseReleaseSequence.load(MemoryOrder::acquire);
        __hidden_common_log::s_RegistrationMutex.unlock();
        __hidden_common_log::s_LeaseReleaseSequence.wait(observedSequence, MemoryOrder::relaxed);
        __hidden_common_log::s_RegistrationMutex.lock();
    }
    --__hidden_common_log::s_WaitingUnregistrations;

    if(afterUnpublish)
        afterUnpublish(callbackContext, registration.previousLogger);
    __hidden_common_log::s_RegistrationMutex.unlock();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

