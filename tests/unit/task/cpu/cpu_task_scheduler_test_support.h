// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "process_exit.h"
#include "task_wait.h"

#include <core/task/cpu/scheduler.h>

#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Watchdog, gates, and capture probes shared by the CPU scheduler suites.
namespace CpuTaskSchedulerTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using CpuTaskTestSupport::ExitTestProcess;

inline constexpr u32 s_GateTimeoutMS = 4000u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeadlineGuard final{
public:
    explicit DeadlineGuard(bool deathTest = false)
        : m_watchdog([deathTest](const StopToken& stop)noexcept{
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS){
                    // A timeout must fail a death test instead of being mistaken for the expected invariant rejection.
                    if(deathTest)
                        ExitTestProcess(0u);
                    TerminateInvariant();
                }
                SleepMS(CpuTaskTestSupport::s_PollIntervalMS);
            }
        }
        )
    {}
    ~DeadlineGuard()noexcept = default;


private:
    JoiningThread m_watchdog;
};


struct TaskGate{
    Atomic<bool> entered{ false };
    Atomic<bool> released{ false };
    Atomic<bool> expired{ false };


public:
    void block()noexcept{
        entered.store(true, MemoryOrder::release);
        if(!CpuTaskTestSupport::WaitUntil(released, s_GateTimeoutMS))
            expired.store(true, MemoryOrder::release);
    }

    void open()noexcept{ released.store(true, MemoryOrder::release); }
};


struct LifetimeProbe{
    Atomic<u32>* retirements;
    u32 value;


public:
    LifetimeProbe(Atomic<u32>& counter, u32 payload)noexcept
        : retirements(&counter)
        , value(payload)
    {}
    LifetimeProbe(const LifetimeProbe&) = delete;
    LifetimeProbe(LifetimeProbe&& other)noexcept
        : retirements(other.retirements)
        , value(other.value)
    {
        other.retirements = nullptr;
    }
    ~LifetimeProbe()noexcept{
        if(retirements)
            retirements->fetch_add(1u, MemoryOrder::release);
    }
};


[[nodiscard]] inline CpuTaskSchedulerConfig HomogeneousWorkers(const u32 count)noexcept{
    CpuTaskSchedulerConfig config;
    config.workerCount = count;
    config.heterogeneous = false;
    return config;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

