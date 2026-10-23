// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_wait_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_WatchdogTimeoutMS = 10000u;
constexpr u32 s_PollIntervalMS = 1u;
constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

class WaitDeadline final{
public:
    WaitDeadline()
        : m_watchdog([](const StopToken& stop)noexcept{
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= __hidden_cpu_task_wait_tests::s_WatchdogTimeoutMS)
                    TerminateInvariant();
                SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
            }
        }
        )
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
    const auto callerTaskAdmission = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(callerTaskAdmission);
    const auto caller = *callerTaskAdmission;
    ASSERT_TRUE(caller.valid());
    const auto cpuTaskAdmission = scope.submit([&]()noexcept{ ++callbacks; }, caller);
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
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
    const auto cpuTaskAdmission = scheduler.submit([&](){
        const auto secondCpuTaskAdmission = scheduler.submit([&](){ scope.wait(); });
        ASSERT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            ASSERT_TRUE(secondCpuTaskAdmission->valid());
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto ancestor = *cpuTaskAdmission;
    ASSERT_TRUE(ancestor.valid());
    const auto middleTaskAdmission = scheduler.submit([]()noexcept{}, ancestor);
    ASSERT_TRUE(middleTaskAdmission);
    const auto middle = *middleTaskAdmission;
    ASSERT_TRUE(middle.valid());
    const auto thirdCpuTaskAdmission = scope.submit([]()noexcept{}, middle);
    ASSERT_TRUE(thirdCpuTaskAdmission);
    if(thirdCpuTaskAdmission)
        ASSERT_TRUE(thirdCpuTaskAdmission->valid());
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
    const auto callerTaskAdmission = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(callerTaskAdmission);
    const auto caller = *callerTaskAdmission;
    ASSERT_TRUE(caller.valid());
    const auto cpuTaskAdmission = scope.submit([&](){
        ++callbacks;
        const auto secondCpuTaskAdmission = scope.submit([&]()noexcept{ ++callbacks; }, caller);
        ASSERT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            ASSERT_TRUE(secondCpuTaskAdmission->valid());
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
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
    const auto callerTaskAdmission = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(callerTaskAdmission);
    const auto caller = *callerTaskAdmission;
    ASSERT_TRUE(caller.valid());
    const auto prerequisiteTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(prerequisiteTaskAdmission);
    const auto prerequisite = *prerequisiteTaskAdmission;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuTaskAdmission = scope.submit([&](){
        ++callbacks;
        const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
        ASSERT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            ASSERT_TRUE(secondCpuTaskAdmission->valid());
    }, prerequisite
    );
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
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
    const auto prerequisiteTaskAdmission = scheduler.submit([&](){ EXPECT_EQ(++sequence, s_ExpectedDualCount); });
    ASSERT_TRUE(prerequisiteTaskAdmission);
    const auto prerequisite = *prerequisiteTaskAdmission;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = scope.submit([&](){
        EXPECT_EQ(++sequence, 1u);
        const auto thirdCpuTaskAdmission = scope.submit([&](){ EXPECT_EQ(++sequence, 3u); }, prerequisite);
        ASSERT_TRUE(thirdCpuTaskAdmission);
        if(thirdCpuTaskAdmission)
            ASSERT_TRUE(thirdCpuTaskAdmission->valid());
    }
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
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
    const auto unrelatedTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(unrelatedTaskAdmission);
    const auto unrelated = *unrelatedTaskAdmission;
    ASSERT_TRUE(unrelated.valid());
    const auto cpuTaskAdmission = scope.submit([&](){
        EXPECT_EQ(callbacks, 0u);
        scheduler.wait(unrelated);
        const auto childTaskAdmission = scheduler.submit([&]()noexcept{ ++callbacks; });
        ASSERT_TRUE(childTaskAdmission);
        const auto child = *childTaskAdmission;
        ASSERT_TRUE(child.valid());
        EXPECT_EQ(child.index, unrelated.index);
        EXPECT_NE(child.generation, unrelated.generation);
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
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
    const auto prerequisiteTaskAdmission = scheduler.submit([&]()noexcept{ ++innerCallbacks; });
    ASSERT_TRUE(prerequisiteTaskAdmission);
    const auto prerequisite = *prerequisiteTaskAdmission;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuTaskAdmission = inner.submit([&]()noexcept{ ++innerCallbacks; }, prerequisite);
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelatedCallbacks; });
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
    const auto thirdCpuTaskAdmission = outer.submit([&](){
        EXPECT_EQ(innerCallbacks, 0u);
        inner.wait();
        EXPECT_EQ(innerCallbacks, s_ExpectedDualCount);
    }
    );
    ASSERT_TRUE(thirdCpuTaskAdmission);
    if(thirdCpuTaskAdmission)
        ASSERT_TRUE(thirdCpuTaskAdmission->valid());
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
    const auto cpuTaskAdmission = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
        scopeA.wait();
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto waiterA = *cpuTaskAdmission;
    const auto secondCpuTaskAdmission = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
        scopeB.wait();
    }
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    const auto waiterB = *secondCpuTaskAdmission;
    ASSERT_TRUE(waiterA.valid());
    ASSERT_TRUE(waiterB.valid());
    while(entered.load(MemoryOrder::acquire) != s_ExpectedDualCount)
        SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    for(u32 index = 0u; index < 32u; ++index){
        const auto prerequisiteBTaskAdmission = scheduler.submit([]()noexcept{});
        ASSERT_TRUE(prerequisiteBTaskAdmission);
        const auto prerequisiteB = *prerequisiteBTaskAdmission;
        const auto prerequisiteATaskAdmission = scheduler.submit([]()noexcept{});
        ASSERT_TRUE(prerequisiteATaskAdmission);
        const auto prerequisiteA = *prerequisiteATaskAdmission;
        EXPECT_TRUE(prerequisiteB.valid());
        EXPECT_TRUE(prerequisiteA.valid());
        const auto thirdCpuTaskAdmission = scopeB.submit([&]()noexcept{ visitsB.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteB);
        EXPECT_TRUE(thirdCpuTaskAdmission);
        if(thirdCpuTaskAdmission)
            EXPECT_TRUE(thirdCpuTaskAdmission->valid());
        const auto fourthCpuTaskAdmission = scopeA.submit([&]()noexcept{ visitsA.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteA);
        EXPECT_TRUE(fourthCpuTaskAdmission);
        if(fourthCpuTaskAdmission)
            EXPECT_TRUE(fourthCpuTaskAdmission->valid());
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
        const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, options);
        ASSERT_TRUE(cpuTaskAdmission);
        if(cpuTaskAdmission)
            ASSERT_TRUE(cpuTaskAdmission->valid());
    }
    for(u32 index = 0u; index < s_ScopedTasks; ++index){
        CpuTaskOptions options;
        options.priority = static_cast<CpuTaskPriority::Enum>(index % s_Priorities);
        const auto secondCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; }, options);
        ASSERT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            ASSERT_TRUE(secondCpuTaskAdmission->valid());
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
    const auto anchorTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(anchorTaskAdmission);
    const auto anchor = *anchorTaskAdmission;
    ASSERT_TRUE(anchor.valid());
    const auto cpuTaskAdmission = scope.submit([&](){
        EXPECT_EQ(unrelated, 0u);
        scheduler.wait(anchor);
        EXPECT_EQ(unrelated, 1u);
        ++joined;
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
    const auto thirdCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; });
    ASSERT_TRUE(thirdCpuTaskAdmission);
    if(thirdCpuTaskAdmission)
        ASSERT_TRUE(thirdCpuTaskAdmission->valid());
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
    }
    );
    const auto cpuTaskAdmission = scheduler.submit([&](){
        waiterEntered.store(true, MemoryOrder::release);
        while(!startWaiter.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
        scope.wait();
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto waiter = *cpuTaskAdmission;
    ASSERT_TRUE(waiter.valid());
    while(!waiterEntered.load(MemoryOrder::acquire))
        SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{
        blockerEntered.store(true, MemoryOrder::release);
        while(!releaseBlocker.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    }
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
    while(!blockerEntered.load(MemoryOrder::acquire))
        SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    const auto thirdCpuTaskAdmission = scheduler.submit([&]()noexcept{
        anchorEntered.store(true, MemoryOrder::release);
        while(!releaseAnchor.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    }
    );
    ASSERT_TRUE(thirdCpuTaskAdmission);
    if(thirdCpuTaskAdmission)
        ASSERT_TRUE(thirdCpuTaskAdmission->valid());
    const auto fourthCpuTaskAdmission = scope.submit([&]()noexcept{
        releaseBlocker.store(true, MemoryOrder::release);
        while(!anchorEntered.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
        joined.fetch_add(1u, MemoryOrder::relaxed);
    }
    );
    ASSERT_TRUE(fourthCpuTaskAdmission);
    if(fourthCpuTaskAdmission)
        ASSERT_TRUE(fourthCpuTaskAdmission->valid());
    const auto fifthCpuTaskAdmission = scheduler.submit([&]()noexcept{ unrelated.fetch_add(1u, MemoryOrder::relaxed); });
    ASSERT_TRUE(fifthCpuTaskAdmission);
    if(fifthCpuTaskAdmission)
        ASSERT_TRUE(fifthCpuTaskAdmission->valid());
    const auto sixthCpuTaskAdmission = scope.submit([&](){
        EXPECT_EQ(unrelated.load(MemoryOrder::acquire), 0u);
        joined.fetch_add(1u, MemoryOrder::relaxed);
        releaseAnchor.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(sixthCpuTaskAdmission);
    if(sixthCpuTaskAdmission)
        ASSERT_TRUE(sixthCpuTaskAdmission->valid());
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
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{
        entered.store(true, MemoryOrder::release);
        while(!release.load(MemoryOrder::acquire))
            SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    }
    );
    ASSERT_TRUE(cpuTaskAdmission);
    const auto prerequisite = *cpuTaskAdmission;
    ASSERT_TRUE(prerequisite.valid());
    while(!entered.load(MemoryOrder::acquire))
        SleepMS(__hidden_cpu_task_wait_tests::s_PollIntervalMS);
    CpuTaskOptions mainThread;
    mainThread.target = CpuTaskTarget::MainThread;
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto secondCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, mainThread);
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
    const auto thirdCpuTaskAdmission = scope.submit([&]()noexcept{
        ++joined;
        release.store(true, MemoryOrder::release);
    }, mainThread
    );
    ASSERT_TRUE(thirdCpuTaskAdmission);
    if(thirdCpuTaskAdmission)
        ASSERT_TRUE(thirdCpuTaskAdmission->valid());
    const auto fourthCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; }, mainThread, &prerequisite, 1u);
    ASSERT_TRUE(fourthCpuTaskAdmission);
    if(fourthCpuTaskAdmission)
        ASSERT_TRUE(fourthCpuTaskAdmission->valid());
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
    const auto rootTaskAdmission = scheduler.submit([&]()noexcept{ ++prerequisites; });
    ASSERT_TRUE(rootTaskAdmission);
    const auto root = *rootTaskAdmission;
    ASSERT_TRUE(root.valid());
    const auto siblingTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(siblingTaskAdmission);
    const auto sibling = *siblingTaskAdmission;
    ASSERT_TRUE(sibling.valid());
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, sibling);
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto positiveTaskAdmission = scheduler.submit([&]()noexcept{ ++prerequisites; }, root);
    ASSERT_TRUE(positiveTaskAdmission);
    const auto positive = *positiveTaskAdmission;
    ASSERT_TRUE(positive.valid());
    const auto secondCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; }, positive);
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
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
    const auto rootTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(rootTaskAdmission);
    const auto root = *rootTaskAdmission;
    ASSERT_TRUE(root.valid());
    const auto siblingTaskAdmission = scheduler.submit([&]()noexcept{ ++prerequisite; }, root);
    ASSERT_TRUE(siblingTaskAdmission);
    const auto sibling = *siblingTaskAdmission;
    ASSERT_TRUE(sibling.valid());
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelatedTail; }, sibling);
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto positiveTaskAdmission = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(positiveTaskAdmission);
    const auto positive = *positiveTaskAdmission;
    ASSERT_TRUE(positive.valid());
    const auto secondCpuTaskAdmission = scope.submit([&](){
        EXPECT_EQ(prerequisite, 0u);
        ++joined;
        const auto thirdCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; }, sibling);
        EXPECT_TRUE(thirdCpuTaskAdmission);
        if(thirdCpuTaskAdmission)
            EXPECT_TRUE(thirdCpuTaskAdmission->valid());
    }, positive
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
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
    const auto rootTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(rootTaskAdmission);
    const auto root = *rootTaskAdmission;
    ASSERT_TRUE(root.valid());
    const auto siblingTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(siblingTaskAdmission);
    const auto sibling = *siblingTaskAdmission;
    ASSERT_TRUE(sibling.valid());
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; }, sibling);
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; }, root);
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
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
    const auto rootTaskAdmission = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(rootTaskAdmission);
    const auto root = *rootTaskAdmission;
    ASSERT_TRUE(root.valid());
    const auto positiveTaskAdmission = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(positiveTaskAdmission);
    const auto positive = *positiveTaskAdmission;
    ASSERT_TRUE(positive.valid());
    const auto cpuTaskAdmission = scope.submit([&](){
        CpuTaskHandle recycled;
        JoiningThread producer([&](){
            const auto recycledTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; });
            ASSERT_TRUE(recycledTaskAdmission);
            recycled =*recycledTaskAdmission;
        });
        producer.join();
        EXPECT_TRUE(recycled.valid());
        EXPECT_EQ(recycled.index, positive.index);
        EXPECT_NE(recycled.generation, positive.generation);
        ++joined;
        const auto secondCpuTaskAdmission = scope.submit([&]()noexcept{ ++joined; });
        EXPECT_TRUE(secondCpuTaskAdmission);
        if(secondCpuTaskAdmission)
            EXPECT_TRUE(secondCpuTaskAdmission->valid());
    }, positive
    );
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
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
    const auto cpuTaskAdmission = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuTaskAdmission);
    if(cpuTaskAdmission)
        ASSERT_TRUE(cpuTaskAdmission->valid());
    const auto secondCpuTaskAdmission = scope.submit([&](){
        for(u32 child = 0u; child < s_Children; ++child){
            const auto thirdCpuTaskAdmission = scheduler.submit([&]()noexcept{ ++children; });
            EXPECT_TRUE(thirdCpuTaskAdmission);
            if(thirdCpuTaskAdmission)
                EXPECT_TRUE(thirdCpuTaskAdmission->valid());
        }
    }
    );
    ASSERT_TRUE(secondCpuTaskAdmission);
    if(secondCpuTaskAdmission)
        ASSERT_TRUE(secondCpuTaskAdmission->valid());
    scope.wait();
    EXPECT_EQ(children, s_Children);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

