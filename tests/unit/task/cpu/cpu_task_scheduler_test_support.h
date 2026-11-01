// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/task/cpu/scheduler.h>

#include <global/atomic.h>
#include <global/platform.h>
#include <global/termination.h>
#include <global/thread.h>
#include <global/timer.h>
#include <global/type.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Watchdog, gates, and capture probes shared by the CPU scheduler suites.
namespace CpuTaskSchedulerTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_PollIntervalMS = 1u;
inline constexpr u32 s_GateTimeoutMS = 4000u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[noreturn]] void ExitTestProcess(const u32 code)noexcept;

[[nodiscard]] b8 WaitUntil(const Atomic<b8>& condition, const u32 timeoutMS)noexcept;


class DeadlineGuard final{
public:
    explicit DeadlineGuard(const b8 deathTest = false, const u32 timeoutMS = s_TestTimeoutMS);
    ~DeadlineGuard()noexcept = default;


private:
    JoiningThread m_watchdog;
};


struct TaskGate{
    Atomic<b8> entered{ false };
    Atomic<b8> released{ false };
    Atomic<b8> expired{ false };


public:
    void block()noexcept;

    void open()noexcept{ released.store(true, MemoryOrder::release); }
};


struct LifetimeProbe{
    Atomic<u32>* retirements;
    u32 value;


public:
    explicit LifetimeProbe(Atomic<u32>& counter, const u32 payload = 0u)noexcept
        : retirements(&counter)
        , value(payload)
    {}
    LifetimeProbe(const LifetimeProbe&) = delete;
    LifetimeProbe(LifetimeProbe&& other)noexcept;
    ~LifetimeProbe()noexcept;
};


[[nodiscard]] CpuTaskSchedulerConfig HomogeneousWorkers(const u32 count)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

