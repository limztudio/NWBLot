// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/alloc/cpu_task.h>
#include <core/common/terminal_entry.h>

#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_cleanup_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core::Alloc;
using NWB::Core::Common::InvokeTerminalEntry;

inline constexpr u32 s_TaskTimeoutMS = 4000u;

struct TerminalTaskError{
    int code;
};

struct RetirementProbe{
    Atomic<u32>* retirements;


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


[[nodiscard]] bool WaitUntil(const Atomic<bool>& condition){
    const Timer begin = TimerNow();
    while(!condition.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_TaskTimeoutMS)
            return false;
        SleepMS(1u);
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskCleanupTests, NormalDestructorsCompleteTasksBeforeRetiringTheirCaptures){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    {
        CpuTaskScheduler scheduler(0u);
        const auto task = scheduler.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
        });
        EXPECT_TRUE(task.valid());
        {
            CpuTaskScope scope(scheduler);
            const auto scopedTask = scope.submit([&, probe = RetirementProbe(retirements)](){
                static_cast<void>(probe);
                ++callbacks;
            });
            EXPECT_TRUE(scopedTask.valid());
        }
        EXPECT_GE(callbacks, 1u);
    }
    EXPECT_EQ(callbacks, 2u);
    EXPECT_EQ(retirements.load(MemoryOrder::acquire), 2u);
}


TEST(CpuTaskCleanupTests, SchedulerDestructorPropagatesCallerFailureToTheTerminalEntry){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    u32 handled = 0u;
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScheduler scheduler(0u);
        EXPECT_TRUE(scheduler.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 17 };
        }, { .target = CpuTaskTarget::MainThread }).valid());
        EXPECT_TRUE(scheduler.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 29 };
        }).valid());
        return 0;
    }, [&](const TerminalTaskError& error){
        ++handled;
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 2u);
        return error.code;
    }, [](){ return -1; });
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
        EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 37 };
        }, { .target = CpuTaskTarget::MainThread }).valid());
        EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 41 };
        }).valid());
        return 0;
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 2u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return error.code;
    }, [](){ return -1; });
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
        EXPECT_TRUE(scheduler.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 53 };
        }, { .target = CpuTaskTarget::MainThread }).valid());
        EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            ++callbacks;
            throw TerminalTaskError{ 59 };
        }).valid());
        throw TerminalTaskError{ 47 };
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 2u);
        return error.code;
    }, [](){ return -1; });
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
    EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
        static_cast<void>(probe);
        started.store(true, MemoryOrder::release);
        if(!WaitUntil(released))
            expired.store(true, MemoryOrder::release);
        finished.store(true, MemoryOrder::release);
    }).valid());
    EXPECT_TRUE(WaitUntil(started));
    EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
        static_cast<void>(probe);
        ++canceledCallbacks;
        throw TerminalTaskError{ 61 };
    }, { .target = CpuTaskTarget::MainThread }).valid());
    JoiningThread releaseWorker([&](){
        if(!WaitUntil(drainStarted))
            expired.store(true, MemoryOrder::release);
        SleepMS(20u);
        released.store(true, MemoryOrder::release);
    });
    drainStarted.store(true, MemoryOrder::release);
    scheduler.drain();
    releaseWorker.join();
    EXPECT_TRUE(finished.load(MemoryOrder::acquire));
    EXPECT_FALSE(expired.load(MemoryOrder::acquire));
    EXPECT_EQ(retirements.load(MemoryOrder::acquire), 2u);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
    EXPECT_FALSE(scheduler.submit([](){}).valid());
}


TEST(CpuTaskCleanupTests, CancelingOneScopeKeepsTheSchedulerAvailableForOtherWork){
    using namespace __hidden_cpu_task_cleanup_tests;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceled(scheduler);
    CpuTaskScope other(scheduler);
    u32 canceledCallbacks = 0u;
    u32 completedCallbacks = 0u;
    EXPECT_TRUE(canceled.submit([&](){ ++canceledCallbacks; }).valid());
    EXPECT_TRUE(other.submit([&](){ ++completedCallbacks; }).valid());
    canceled.cancel();
    canceled.wait();
    other.wait();
    const auto later = scheduler.submit([&](){ ++completedCallbacks; });
    ASSERT_TRUE(later.valid());
    scheduler.wait(later);
    EXPECT_EQ(canceledCallbacks, 0u);
    EXPECT_EQ(completedCallbacks, 2u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskCleanupTests, Queuing1024TasksBeforeExecutionGrowsSchedulerStorage){
    using namespace __hidden_cpu_task_cleanup_tests;
    static constexpr usize s_TaskCount = 1024u;
    u32 visits[s_TaskCount] = {};
    CpuTaskScheduler scheduler(0u);
    for(usize index = 0u; index < s_TaskCount; ++index){
        ASSERT_TRUE(scheduler.submit([&, index](){ ++visits[index]; }).valid());
    }
    EXPECT_EQ(scheduler.statistics().outstandingTasks, s_TaskCount);
    scheduler.wait();
    for(const u32 count : visits)
        EXPECT_EQ(count, 1u);
    EXPECT_EQ(scheduler.statistics().completedTasks, s_TaskCount);
}


TEST(CpuTaskCleanupTests, ConcurrentProducersCompleteNestedTaskRanges){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> visits{ 0u };
    CpuTaskSchedulerConfig config;
    config.workerCount = 2u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    for(u32 producer = 0u; producer < 2u; ++producer){
        ASSERT_TRUE(scope.submit([&](){
            scheduler.parallelFor(0u, 128u, 4u, [&](usize){
                scheduler.parallelFor(0u, 32u, 4u, [&](usize){
                    visits.fetch_add(1u, MemoryOrder::relaxed);
                });
            });
        }).valid());
    }
    scope.wait();
    EXPECT_EQ(visits.load(MemoryOrder::acquire), 2u * 128u * 32u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskCleanupTests, NestedRangeFailureJoinsItsSubtreeBeforeUnwindingTheReceivingScope){
    using namespace __hidden_cpu_task_cleanup_tests;
    Atomic<u32> retirements{ 0u };
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    const int result = InvokeTerminalEntry<TerminalTaskError>([&]()->int{
        CpuTaskScope scope(scheduler);
        EXPECT_TRUE(scope.submit([&, probe = RetirementProbe(retirements)](){
            static_cast<void>(probe);
            scope.parallelFor(0u, 16u, 1u, [&](usize){
                ++callbacks;
                throw TerminalTaskError{ 67 };
            });
        }).valid());
        scope.wait();
        return 0;
    }, [&](const TerminalTaskError& error){
        EXPECT_EQ(retirements.load(MemoryOrder::acquire), 1u);
        EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
        return error.code;
    }, [](){ return -1; });
    EXPECT_EQ(result, 67);
    EXPECT_EQ(callbacks, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

