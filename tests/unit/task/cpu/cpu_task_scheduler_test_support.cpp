// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#elif defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CpuTaskSchedulerTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[noreturn]] void ExitTestProcess([[maybe_unused]] const u32 code)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    ExitProcess(code);
#elif defined(NWB_PLATFORM_LINUX)
    ::_exit(static_cast<int>(code));
#else
    TerminateInvariant();
#endif
}

[[nodiscard]] b8 WaitUntil(const Atomic<b8>& condition, const u32 timeoutMS)noexcept{
    const Timer begin = TimerNow();
    while(!condition.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), begin) >= timeoutMS)
            return false;
        SleepMS(s_PollIntervalMS);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


DeadlineGuard::DeadlineGuard(const b8 deathTest, const u32 timeoutMS)
    : m_watchdog([deathTest, timeoutMS](const StopToken& stop)noexcept{
        const Timer begin = TimerNow();
        while(!stop.stop_requested()){
            if(DurationInMS<u64>(TimerNow(), begin) >= timeoutMS){
                // A timeout must fail a death test instead of being mistaken for the expected invariant rejection.
                if(deathTest)
                    ExitTestProcess(0u);
                TerminateInvariant();
            }
            SleepMS(s_PollIntervalMS);
        }
    }
    )
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void TaskGate::block()noexcept{
    entered.store(true, MemoryOrder::release);
    if(!WaitUntil(released, s_GateTimeoutMS))
        expired.store(true, MemoryOrder::release);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


LifetimeProbe::LifetimeProbe(LifetimeProbe&& other)noexcept
    : retirements(other.retirements)
    , value(other.value)
{
    other.retirements = nullptr;
}

LifetimeProbe::~LifetimeProbe()noexcept{
    if(retirements)
        retirements->fetch_add(1u, MemoryOrder::release);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] CpuTaskSchedulerConfig HomogeneousWorkers(const u32 count)noexcept{
    CpuTaskSchedulerConfig config;
    config.workerCount = count;
    config.heterogeneous = false;
    return config;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

