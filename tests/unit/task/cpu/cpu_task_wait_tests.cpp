// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_wait_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


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
    ASSERT_TRUE(scope.submit([&]()noexcept{ ++callbacks; }, caller).valid());
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
    const auto middle = scheduler.submit([]()noexcept{}, ancestor);
    ASSERT_TRUE(middle.valid());
    ASSERT_TRUE(scope.submit([]()noexcept{}, middle).valid());
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
        ASSERT_TRUE(scope.submit([&]()noexcept{ ++callbacks; }, caller).valid());
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
    const auto prerequisite = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(scope.submit([&](){
        ++callbacks;
        ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++callbacks; }).valid());
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
    const auto prerequisite = scheduler.submit([&](){ EXPECT_EQ(++sequence, s_ExpectedDualCount); });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }).valid());
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
    const auto unrelated = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(unrelated.valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(callbacks, 0u);
        scheduler.wait(unrelated);
        const auto child = scheduler.submit([&]()noexcept{ ++callbacks; });
        ASSERT_TRUE(child.valid());
        EXPECT_EQ(child.index, unrelated.index);
        EXPECT_NE(child.generation, unrelated.generation);
    }).valid());
    scope.wait();
    EXPECT_EQ(callbacks, s_ExpectedDualCount);
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
    const auto prerequisite = scheduler.submit([&]()noexcept{ ++innerCallbacks; });
    ASSERT_TRUE(prerequisite.valid());
    ASSERT_TRUE(inner.submit([&]()noexcept{ ++innerCallbacks; }, prerequisite).valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelatedCallbacks; }).valid());
    ASSERT_TRUE(outer.submit([&](){
        EXPECT_EQ(innerCallbacks, 0u);
        inner.wait();
        EXPECT_EQ(innerCallbacks, s_ExpectedDualCount);
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
    config.workerCount = s_ExpectedDualCount;
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
    while(entered.load(MemoryOrder::acquire) != s_ExpectedDualCount)
        SleepMS(1u);
    for(u32 index = 0u; index < 32u; ++index){
        const auto prerequisiteB = scheduler.submit([]()noexcept{});
        const auto prerequisiteA = scheduler.submit([]()noexcept{});
        EXPECT_TRUE(prerequisiteB.valid());
        EXPECT_TRUE(prerequisiteA.valid());
        EXPECT_TRUE(scopeB.submit([&]()noexcept{ visitsB.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteB).valid());
        EXPECT_TRUE(scopeA.submit([&]()noexcept{ visitsA.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteA).valid());
    }
    start.store(true, MemoryOrder::release);
    scheduler.wait(waiterA);
    scheduler.wait(waiterB);
    EXPECT_EQ(visitsA.load(MemoryOrder::acquire), 32u);
    EXPECT_EQ(visitsB.load(MemoryOrder::acquire), 32u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, ScopeJoinSkipsUnrelatedPrefixesAcrossPriorityQueues){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    constexpr u32 s_UnrelatedTasks = 1024u;
    constexpr u32 s_ScopedTasks = 128u;
    constexpr u32 s_Priorities = 3u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelated = 0u;
    u32 joined = 0u;
    for(u32 index = 0u; index < s_UnrelatedTasks; ++index){
        CpuTaskOptions options;
        options.priority = static_cast<CpuTaskPriority::Enum>(index % s_Priorities);
        ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }, options).valid());
    }
    for(u32 index = 0u; index < s_ScopedTasks; ++index){
        CpuTaskOptions options;
        options.priority = static_cast<CpuTaskPriority::Enum>(index % s_Priorities);
        ASSERT_TRUE(scope.submit([&]()noexcept{ ++joined; }, options).valid());
    }
    scope.wait();
    EXPECT_EQ(joined, s_ScopedTasks);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, s_UnrelatedTasks);
}

TEST(CpuTaskWaitTests, NestedJoinCanRetireAnUnrelatedAnchorWithoutNewPublication){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto anchor = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(anchor.valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(unrelated, 0u);
        scheduler.wait(anchor);
        EXPECT_EQ(unrelated, 1u);
        ++joined;
    }).valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }).valid());
    ASSERT_TRUE(scope.submit([&]()noexcept{ ++joined; }).valid());
    scope.wait();
    EXPECT_EQ(joined, s_ExpectedDualCount);
    EXPECT_EQ(unrelated, 1u);
    scheduler.wait();
    EXPECT_EQ(unrelated, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, AnotherWorkerCanClaimAnUnrelatedAnchorDuringScopeExecution){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    Atomic<bool> waiterEntered{ false };
    Atomic<bool> blockerEntered{ false };
    Atomic<bool> startWaiter{ false };
    Atomic<bool> releaseBlocker{ false };
    Atomic<bool> anchorEntered{ false };
    Atomic<bool> releaseAnchor{ false };
    Atomic<u32> joined{ 0u };
    Atomic<u32> unrelated{ 0u };
    CpuTaskSchedulerConfig config;
    config.workerCount = s_ExpectedDualCount;
    config.heterogeneous = false;
    CpuTaskScheduler scheduler(config);
    CpuTaskScope scope(scheduler);
    ScopeExit release([&]()noexcept{
        startWaiter.store(true, MemoryOrder::release);
        releaseBlocker.store(true, MemoryOrder::release);
        releaseAnchor.store(true, MemoryOrder::release);
    });
    const auto waiter = scheduler.submit([&](){
        waiterEntered.store(true, MemoryOrder::release);
        while(!startWaiter.load(MemoryOrder::acquire))
            SleepMS(1u);
        scope.wait();
    });
    ASSERT_TRUE(waiter.valid());
    while(!waiterEntered.load(MemoryOrder::acquire))
        SleepMS(1u);
    ASSERT_TRUE(scheduler.submit([&](){
        blockerEntered.store(true, MemoryOrder::release);
        while(!releaseBlocker.load(MemoryOrder::acquire))
            SleepMS(1u);
    }).valid());
    while(!blockerEntered.load(MemoryOrder::acquire))
        SleepMS(1u);
    ASSERT_TRUE(scheduler.submit([&](){
        anchorEntered.store(true, MemoryOrder::release);
        while(!releaseAnchor.load(MemoryOrder::acquire))
            SleepMS(1u);
    }).valid());
    ASSERT_TRUE(scope.submit([&](){
        releaseBlocker.store(true, MemoryOrder::release);
        while(!anchorEntered.load(MemoryOrder::acquire))
            SleepMS(1u);
        joined.fetch_add(1u, MemoryOrder::relaxed);
    }).valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ unrelated.fetch_add(1u, MemoryOrder::relaxed); }).valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(unrelated.load(MemoryOrder::acquire), 0u);
        joined.fetch_add(1u, MemoryOrder::relaxed);
        releaseAnchor.store(true, MemoryOrder::release);
    }).valid());
    startWaiter.store(true, MemoryOrder::release);
    scheduler.wait(waiter);
    scheduler.wait();
    EXPECT_EQ(joined.load(MemoryOrder::acquire), s_ExpectedDualCount);
    EXPECT_EQ(unrelated.load(MemoryOrder::acquire), 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}

TEST(CpuTaskWaitTests, DependencyCompletionAppendsScopedWorkAfterAnUnrelatedAnchor){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    Atomic<bool> entered{ false };
    Atomic<bool> release{ false };
    CpuTaskScheduler scheduler(1u);
    CpuTaskScope scope(scheduler);
    ScopeExit releaseOnFailure([&]()noexcept{ release.store(true, MemoryOrder::release); });
    const auto prerequisite = scheduler.submit([&](){
        entered.store(true, MemoryOrder::release);
        while(!release.load(MemoryOrder::acquire))
            SleepMS(1u);
    });
    ASSERT_TRUE(prerequisite.valid());
    while(!entered.load(MemoryOrder::acquire))
        SleepMS(1u);
    CpuTaskOptions mainThread;
    mainThread.target = CpuTaskTarget::MainThread;
    u32 unrelated = 0u;
    u32 joined = 0u;
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }, mainThread).valid());
    ASSERT_TRUE(scope.submit([&]()noexcept{
        ++joined;
        release.store(true, MemoryOrder::release);
    }, mainThread).valid());
    ASSERT_TRUE(scope.submit([&]()noexcept{ ++joined; }, mainThread, &prerequisite, 1u).valid());
    scope.wait();
    EXPECT_EQ(joined, s_ExpectedDualCount);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}


TEST(CpuTaskWaitTests, APositiveSearchCannotAdmitVisitedUnrelatedSiblingBranches){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 prerequisites = 0u;
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto root = scheduler.submit([&]()noexcept{ ++prerequisites; });
    ASSERT_TRUE(root.valid());
    const auto sibling = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(sibling.valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }, sibling).valid());
    const auto positive = scheduler.submit([&]()noexcept{ ++prerequisites; }, root);
    ASSERT_TRUE(positive.valid());
    ASSERT_TRUE(scope.submit([&]()noexcept{ ++joined; }, positive).valid());
    scope.wait();
    EXPECT_EQ(prerequisites, s_ExpectedDualCount);
    EXPECT_EQ(joined, 1u);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, s_ExpectedDualCount);
}

TEST(CpuTaskWaitTests, PublicationCanMakeAnEarlierNegativeSiblingContribute){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelatedTail = 0u;
    u32 prerequisite = 0u;
    u32 joined = 0u;
    const auto root = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(root.valid());
    const auto sibling = scheduler.submit([&]()noexcept{ ++prerequisite; }, root);
    ASSERT_TRUE(sibling.valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelatedTail; }, sibling).valid());
    const auto positive = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(positive.valid());
    ASSERT_TRUE(scope.submit([&](){
        EXPECT_EQ(prerequisite, 0u);
        ++joined;
        EXPECT_TRUE(scope.submit([&]()noexcept{ ++joined; }, sibling).valid());
    }, positive).valid());
    scope.wait();
    EXPECT_EQ(prerequisite, 1u);
    EXPECT_EQ(joined, s_ExpectedDualCount);
    EXPECT_EQ(unrelatedTail, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelatedTail, 1u);
}


TEST(CpuTaskWaitTests, AShortestScopeProofDoesNotAdmitUnprocessedSiblingFrontiers){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto root = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(root.valid());
    const auto sibling = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(sibling.valid());
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }, sibling).valid());
    ASSERT_TRUE(scope.submit([&]()noexcept{ ++joined; }, root).valid());
    scope.wait();
    EXPECT_EQ(joined, 1u);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, s_ExpectedDualCount);
}

TEST(CpuTaskWaitTests, RecycledPositivePrerequisiteSlotsDoNotAdmitUnrelatedWork){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto root = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(root.valid());
    const auto positive = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(positive.valid());
    ASSERT_TRUE(scope.submit([&](){
        CpuTaskHandle recycled;
        JoiningThread producer([&](){ recycled = scheduler.submit([&](){ ++unrelated; }); });
        producer.join();
        EXPECT_TRUE(recycled.valid());
        EXPECT_EQ(recycled.index, positive.index);
        EXPECT_NE(recycled.generation, positive.generation);
        ++joined;
        EXPECT_TRUE(scope.submit([&]()noexcept{ ++joined; }).valid());
    }, positive).valid());
    scope.wait();
    EXPECT_EQ(joined, s_ExpectedDualCount);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}

TEST(CpuTaskWaitTests, StructuredParentCompletionStaysScopedUntilEveryChildRetires){
    using namespace __hidden_cpu_task_wait_tests;
    WaitDeadline deadline;
    constexpr u32 s_Children = 64u;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 unrelated = 0u;
    u32 children = 0u;
    ASSERT_TRUE(scheduler.submit([&]()noexcept{ ++unrelated; }).valid());
    ASSERT_TRUE(scope.submit([&](){
        for(u32 child = 0u; child < s_Children; ++child)
            EXPECT_TRUE(scheduler.submit([&]()noexcept{ ++children; }).valid());
    }).valid());
    scope.wait();
    EXPECT_EQ(children, s_Children);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

