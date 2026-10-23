// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <loader/callback_shutdown.h>

#include <core/task/cpu/scheduler.h>

#include <global/terminal_entry.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_cleanup_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_PollIntervalMS = 1u;
constexpr u32 s_CleanupWaitIntervalMS = 20u;

constexpr u32 s_ExpectedDualCount = 2u;
constexpr int s_LoaderShutdownFailure = 73;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_TaskTimeoutMS = 4000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct TerminalTaskError{
    int code;
};

struct RetirementProbe{
    Atomic<u32>* retirements;


public:
    explicit RetirementProbe(Atomic<u32>& counter)noexcept
        : retirements(&counter)
    {}
    RetirementProbe(const RetirementProbe&) = delete;
    RetirementProbe(RetirementProbe&& other)noexcept
        : retirements(other.retirements)
    {
        other.retirements = nullptr;
    }
    ~RetirementProbe()noexcept{
        if(retirements)
            retirements->fetch_add(1u, MemoryOrder::release);
    }
};


struct ReentrantRetirementProbe{
    CpuTaskScheduler& m_scheduler;
    const CpuTaskHandle& m_handle;
    bool& m_incomplete;
    usize& m_outstanding;
    bool m_active = true;


public:
    ReentrantRetirementProbe(CpuTaskScheduler& scheduler, const CpuTaskHandle& handle, bool& incomplete, usize& outstanding)noexcept
        : m_scheduler(scheduler)
        , m_handle(handle)
        , m_incomplete(incomplete)
        , m_outstanding(outstanding)
    {}
    ReentrantRetirementProbe(const ReentrantRetirementProbe&) = delete;
    ReentrantRetirementProbe(ReentrantRetirementProbe&& other)noexcept
        : m_scheduler(other.m_scheduler)
        , m_handle(other.m_handle)
        , m_incomplete(other.m_incomplete)
        , m_outstanding(other.m_outstanding)
        , m_active(other.m_active)
    {
        other.m_active = false;
    }
    ~ReentrantRetirementProbe()noexcept{
        if(m_active){
            m_incomplete = !m_scheduler.isComplete(m_handle);
            m_outstanding = m_scheduler.statistics().outstandingTasks;
        }
    }
};


struct CallbackLoanRetirementProbe final : NoCopy{
    bool& ownerAlive;
    Atomic<u32>& retirements;
    Atomic<u32>& lateRetirements;
    bool active = true;


public:
    CallbackLoanRetirementProbe(bool& alive, Atomic<u32>& retired, Atomic<u32>& late)noexcept
        : ownerAlive(alive)
        , retirements(retired)
        , lateRetirements(late)
    {}
    CallbackLoanRetirementProbe(CallbackLoanRetirementProbe&& other)noexcept
        : ownerAlive(other.ownerAlive)
        , retirements(other.retirements)
        , lateRetirements(other.lateRetirements)
        , active(other.active)
    {
        other.active = false;
    }
    ~CallbackLoanRetirementProbe()noexcept{
        if(!active)
            return;
        retirements.fetch_add(1u, MemoryOrder::release);
        if(!ownerAlive)
            lateRetirements.fetch_add(1u, MemoryOrder::release);
    }
};

class ShutdownSubmittingCallbacks final : public NWB::IProjectEntryCallbacks{
public:
    ShutdownSubmittingCallbacks(CpuTaskScope& tasks, bool& ownerAlive, Atomic<u32>& retirements, Atomic<u32>& lateRetirements, u32& callbacks)noexcept
        : m_tasks(tasks)
        , m_ownerAlive(ownerAlive)
        , m_retirements(retirements)
        , m_lateRetirements(lateRetirements)
        , m_callbacks(callbacks)
    {}
    virtual ~ShutdownSubmittingCallbacks()noexcept override{ m_ownerAlive = false; }


public:
    virtual void onShutdown()override{
        const auto first = m_tasks.submit([this](){
            ++m_callbacks;
            throw TerminalTaskError{ s_LoaderShutdownFailure };
        }, { .target = CpuTaskTarget::MainThread }
        );
        EXPECT_TRUE(first);
        if(!first)
            return;
        const CpuTaskHandle dependency = *first;
        const auto second = m_tasks.submit([this, probe = CallbackLoanRetirementProbe(m_ownerAlive, m_retirements, m_lateRetirements)]()noexcept{
            if(probe.active && probe.ownerAlive)
                ++m_callbacks;
        }, { .target = CpuTaskTarget::MainThread }, &dependency, 1u
        );
        EXPECT_TRUE(second);
    }


private:
    CpuTaskScope& m_tasks;
    bool& m_ownerAlive;
    Atomic<u32>& m_retirements;
    Atomic<u32>& m_lateRetirements;
    u32& m_callbacks;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool WaitUntil(const Atomic<bool>& condition)noexcept{
    const Timer begin = TimerNow();
    while(!condition.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_TaskTimeoutMS)
            return false;
        SleepMS(__hidden_cpu_task_cleanup_tests::s_PollIntervalMS);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskCleanupTests, SchedulerDestructorPropagatesCallerFailureToTheTerminalEntry){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    u32 handled = 0u;
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScheduler scheduler(0u);
        const auto cpuTaskAdmission = scheduler.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 17 };
        }, { .target = CpuTaskTarget::MainThread }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        const auto secondCpuTaskAdmission = scheduler.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 29 };
        }
        );
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
        return 0;
    }, [&](const TerminalTaskError& error){
        ++handled;
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), s_ExpectedDualCount);
        return error.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, 17);
    EXPECT_EQ(handled, 1u);
    EXPECT_EQ(callbacks, 1u);
}


TEST(CpuTaskCleanupTests, ScopeDestructorPropagatesCallerFailureAfterDrainingItsTasks){
    using namespace __hidden_cpu_task_cleanup_tests;
    CpuTaskScheduler scheduler(0u);
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScope scope(scheduler);
        const auto cpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 37 };
        }, { .target = CpuTaskTarget::MainThread }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        const auto secondCpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 41 };
        }
        );
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
        return 0;
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), s_ExpectedDualCount);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return error.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, 37);
    EXPECT_EQ(callbacks, 1u);
}


TEST(CpuTaskCleanupTests, UnrelatedUnwindCancelsQueuedTasksWithoutReplacingTheOriginalFailure){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScheduler scheduler(0u);
        CpuTaskScope scope(scheduler);
        const auto cpuTaskAdmission = scheduler.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 53 };
        }, { .target = CpuTaskTarget::MainThread }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        const auto secondCpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            ++callbacks;
            throw TerminalTaskError{ 59 };
        }
        );
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
        throw TerminalTaskError{ 47 };
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), s_ExpectedDualCount);
        return error.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, 47);
    EXPECT_EQ(callbacks, 0u);
}


TEST(CpuTaskCleanupTests, TerminalDrainJoinsAnActiveWorkerAndRetiresCanceledCaptures){
    using namespace __hidden_cpu_task_cleanup_tests;
    CpuTaskSchedulerConfig config;
    config.workerCount = 1u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    Atomic<u32> retirements{ 0u };
    Atomic<bool> started{ false };
    Atomic<bool> drainStarted{ false };
    Atomic<bool> released{ false };
    Atomic<bool> finished{ false };
    Atomic<bool> expired{ false };
    u32 canceledCallbacks = 0u;
    const auto cpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
        EXPECT_TRUE(probe.retirements != nullptr);
        started.store(true, MemoryOrder::release);
        if(!WaitUntil(released))
            expired.store(true, MemoryOrder::release);
        finished.store(true, MemoryOrder::release);
    }
    );
    EXPECT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        EXPECT_TRUE(cpuTaskAdmission->valid());
    EXPECT_TRUE(WaitUntil(started));
    const auto secondCpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
        EXPECT_TRUE(probe.retirements != nullptr);
        ++canceledCallbacks;
        throw TerminalTaskError{ 61 };
    }, { .target = CpuTaskTarget::MainThread }
    );
    EXPECT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        EXPECT_TRUE(secondCpuTaskAdmission->valid());
    JoiningThread releaseWorker([&]()noexcept{
        if(!WaitUntil(drainStarted))
            expired.store(true, MemoryOrder::release);
        SleepMS(s_CleanupWaitIntervalMS);
        released.store(true, MemoryOrder::release);
    }
    );
    drainStarted.store(true, MemoryOrder::release);
    scheduler.drain();
    releaseWorker.join();
    EXPECT_TRUE(finished.load(MemoryOrder::acquire));
    EXPECT_FALSE(expired.load(MemoryOrder::acquire));
    EXPECT_EQ(retirements.load(MemoryOrder::acquire), s_ExpectedDualCount);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    const auto thirdCpuTaskAdmission = scheduler.submit([]()noexcept{});
    EXPECT_FALSE(thirdCpuTaskAdmission);
}


TEST(CpuTaskCleanupTests, CancelingOneScopeKeepsTheSchedulerAvailableForOtherWork){
    using namespace __hidden_cpu_task_cleanup_tests;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceled(scheduler);
    CpuTaskScope other(scheduler);
    u32 canceledCallbacks = 0u;
    u32 completedCallbacks = 0u;
    const auto cpuTaskAdmission = canceled.submit([&]()noexcept{ ++canceledCallbacks; });
    EXPECT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        EXPECT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = other.submit([&]()noexcept{ ++completedCallbacks; });
    EXPECT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        EXPECT_TRUE(secondCpuTaskAdmission->valid());
    canceled.cancel();
    canceled.wait();
    other.wait();
    const auto laterTaskAdmission = scheduler.submit([&]()noexcept{ ++completedCallbacks; });
    ASSERT_TRUE(laterTaskAdmission);
    const auto later = *laterTaskAdmission;
    ASSERT_TRUE(later.valid());
    scheduler.wait(later);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(completedCallbacks, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskCleanupTests, ConcurrentProducersCompleteNestedTaskRanges){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> visits{ 0u };
    CpuTaskSchedulerConfig config;
    config.workerCount = s_ExpectedDualCount;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    for(u32 producer = 0u; producer < s_ExpectedDualCount; ++producer){
        const auto cpuTaskAdmission = scope.submit([&](){
            EXPECT_TRUE((scheduler.parallelFor(0u, 128u, 4u, [&](usize){
                EXPECT_TRUE((scheduler.parallelFor(0u, 32u, 4u, [&](usize)noexcept{
                    visits.fetch_add(1u, MemoryOrder::relaxed);
                }
                )));
            }
            )));
        }
        );
        ASSERT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            ASSERT_TRUE(cpuTaskAdmission->valid());
    }
    scope.wait();
    EXPECT_EQ(visits.load(MemoryOrder::acquire), s_ExpectedDualCount * 128u * 32u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskCleanupTests, NestedRangeFailureJoinsItsSubtreeBeforeUnwindingTheReceivingScope){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScope scope(scheduler);
        const auto cpuTaskAdmission = scope.submit([&, probe = RetirementProbe(retirements)](){
            EXPECT_TRUE(probe.retirements != nullptr);
            EXPECT_TRUE((scope.parallelFor(0u, 16u, 1u, [&](usize){
                ++callbacks;
                throw TerminalTaskError{ 67 };
            }
            )));
        }
        );
        EXPECT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            EXPECT_TRUE(cpuTaskAdmission->valid());
        scope.wait();
        return 0;
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 1u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return error.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, 67);
    EXPECT_EQ(callbacks, 1u);
}


TEST(CpuTaskCleanupTests, DescendantRetirementCanReenterSchedulerBeforeParentCompletionPublishes){
    using namespace __hidden_cpu_task_cleanup_tests;
    JoiningThread watchdog([](const StopToken& stop)noexcept{
        const Timer begin = TimerNow();
        while(!stop.stop_requested()){
            if(DurationInMS<u64>(TimerNow(), begin) >= s_TaskTimeoutMS)
                TerminateInvariant();
            SleepMS(__hidden_cpu_task_cleanup_tests::s_PollIntervalMS);
        }
    }
    );
    CpuTaskHandle parent;
    bool incompleteDuringRetirement = false;
    usize outstandingDuringRetirement = 0u;
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    const auto cpuTaskAdmission = scheduler.submit([&scheduler, &callbacks, probe = ReentrantRetirementProbe(scheduler, parent, incompleteDuringRetirement, outstandingDuringRetirement)](){
        EXPECT_TRUE(probe.m_active);
        const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    parent = *cpuTaskAdmission;
    ASSERT_TRUE(parent.valid());
    const auto thirdCpuTaskAdmission = scheduler.submit([&](){
        EXPECT_TRUE(incompleteDuringRetirement);
        EXPECT_EQ(outstandingDuringRetirement, s_ExpectedDualCount);
        ++callbacks;
    }, parent
    );
    ASSERT_TRUE(thirdCpuTaskAdmission);
    const auto dependent = *thirdCpuTaskAdmission;
    ASSERT_TRUE(dependent.valid());
    scheduler.wait(dependent);
    EXPECT_EQ(callbacks, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskCleanupTests, LoaderShutdownJoinsPostCallbackTasksBeforeCallbackOwnerExpires){
    using namespace __hidden_cpu_task_cleanup_tests;
    bool ownerAlive = true;
    Atomic<u32> retirements{ 0u };
    Atomic<u32> lateRetirements{ 0u };
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        ShutdownSubmittingCallbacks owner(scope, ownerAlive, retirements, lateRetirements, callbacks);
        NWB::LoaderDetail::CallbackShutdownGuard shutdown(owner, scope);
        shutdown.activate();
        return 0;
    }, [&](const TerminalTaskError& error){
        EXPECT_FALSE(ownerAlive);
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 1u);
        EXPECT_EQ(lateRetirements.load(MemoryOrder::acquire), 0u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return error.code;
    }, []()noexcept{ return -1; }
    );
    EXPECT_EQ(result, s_LoaderShutdownFailure);
    EXPECT_EQ(callbacks, 1u);
    scope.drain();
    EXPECT_EQ(retirements.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(lateRetirements.load(MemoryOrder::acquire), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

