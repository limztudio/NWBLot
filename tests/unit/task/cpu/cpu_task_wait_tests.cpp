// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_wait_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class WaitDeadline final{
public:
    WaitDeadline()
        : m_watchdog([](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= 10000u)
                    TerminateInvariant();
                SleepMS(1u);
            }
        })
    {}


private:
    JoiningThread m_watchdog;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskWaitTests, ScopeRejectsAnIndependentTaskDependingOnTheCaller){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    ASSERT_TRUE(scope.submit([&](){ ++callbacks; }, caller).valid());
    EXPECT_THROW(scheduler.wait(caller), RuntimeException);
    scope.wait();
    EXPECT_EQ(callbacks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeRejectsTransitiveDependentsOfTheCallersAncestor){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    const auto ancestor = scheduler.submit([&](){
        ASSERT_TRUE(scheduler.submit([&](){ scope.wait(); }).valid());
    });
    ASSERT_TRUE(ancestor.valid());
    const auto middle = scheduler.submit([](){}, ancestor);
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(scope.submit([](){}, middle).valid());
    EXPECT_THROW(scheduler.wait(ancestor), RuntimeException);
    scope.wait();
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeRechecksDependenciesPublishedByCooperativeWork){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    ASSERT_TRUE(scope.submit([&](){
        ++callbacks;
        ASSERT_TRUE(scope.submit([&](){ ++callbacks; }, caller).valid());
    }).valid());
    EXPECT_THROW(scheduler.wait(caller), RuntimeException);
    scope.wait();
    EXPECT_EQ(callbacks, 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeCanJoinIndependentPrerequisitesAndDescendants){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 callbacks = 0u;
    const auto caller = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(caller.valid());
    const auto prerequisite = scheduler.submit([&](){ ++callbacks; });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(scope.submit([&](){
        ++callbacks;
        ASSERT_TRUE(scheduler.submit([&](){ ++callbacks; }).valid());
    }, prerequisite).valid());
    scheduler.wait(caller);
    scope.wait();
    EXPECT_EQ(callbacks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskWaitTests, NewDependencyInvalidatesAnEarlierUnrelatedSearch){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    u32 sequence = 0u;
    u32 unrelated = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    const auto prerequisite = scheduler.submit([&](){ EXPECT_EQ(++sequence, 2u); });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(scheduler.submit([&](){ ++unrelated; }).valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(++sequence, 1u);
        ASSERT_TRUE(scope.submit([&](){ EXPECT_EQ(++sequence, 3u); }, prerequisite).valid());
    }).valid());
    scope.wait();
    EXPECT_EQ(sequence, 3u);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}

TEST(CpuTaskWaitTests, ReusedUnrelatedSlotCanBecomeAScopedDescendant){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    u32 callbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    const auto unrelated = scheduler.submit([&](){ ++callbacks; });
    ASSERT_TRUE(unrelated.valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(callbacks, 0u);
        scheduler.wait(unrelated);
        const auto child = scheduler.submit([&](){ ++callbacks; });
        ASSERT_TRUE(child.valid());
        EXPECT_EQ(child.index, unrelated.index);
        EXPECT_NE(child.generation, unrelated.generation);
    }).valid());
    scope.wait();
    EXPECT_EQ(callbacks, 2u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, NestedScopesUseIndependentSearchIdentities){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    u32 innerCallbacks = 0u;
    u32 unrelatedCallbacks = 0u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope inner(scheduler);
    CpuTaskScope outer(scheduler);
    const auto prerequisite = scheduler.submit([&](){ ++innerCallbacks; });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(inner.submit([&](){ ++innerCallbacks; }, prerequisite).valid());
    ASSERT_TRUE(scheduler.submit([&](){ ++unrelatedCallbacks; }).valid());
    ASSERT_TRUE(outer.submit([&](){
        EXPECT_EQ(innerCallbacks, 0u);
        inner.wait();
        EXPECT_EQ(innerCallbacks, 2u);
    }).valid());
    outer.wait();
    EXPECT_EQ(unrelatedCallbacks, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelatedCallbacks, 1u);
}

TEST(CpuTaskWaitTests, ConcurrentScopeWaitersCanSwitchTheSharedSearchCache){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    Atomic<u32> entered{ 0u };
    Atomic<bool> start{ false };
    Atomic<u32> visitsA{ 0u };
    Atomic<u32> visitsB{ 0u };
    CpuTaskSchedulerConfig config;
    config.workerCount = 2u;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scopeA(scheduler);
    CpuTaskScope scopeB(scheduler);
    const auto waiterA = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(1u);
        scopeA.wait();
    });
    const auto waiterB = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(1u);
        scopeB.wait();
    });
    ASSERT_TRUE(waiterA.valid());
    ASSERT_TRUE(waiterB.valid());
    while(entered.load(MemoryOrder::acquire) != 2u)
        SleepMS(1u);
    for(u32 index = 0u; index < 32u; ++index){
        const auto prerequisiteB = scheduler.submit([](){});
        const auto prerequisiteA = scheduler.submit([](){});
        EXPECT_TRUE(prerequisiteB.valid());
        EXPECT_TRUE(prerequisiteA.valid());
        EXPECT_TRUE(scopeB.submit([&](){ visitsB.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteB).valid());
        EXPECT_TRUE(scopeA.submit([&](){ visitsA.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteA).valid());
    }
    start.store(true, MemoryOrder::release);
    scheduler.wait(waiterA);
    scheduler.wait(waiterB);
    EXPECT_EQ(visitsA.load(MemoryOrder::acquire), 32u);
    EXPECT_EQ(visitsB.load(MemoryOrder::acquire), 32u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

