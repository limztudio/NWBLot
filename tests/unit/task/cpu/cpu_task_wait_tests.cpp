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
    const auto cpuAdmission136 = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(cpuAdmission136);
    const auto caller = *cpuAdmission136;
    ASSERT_TRUE(caller.valid());
    const auto cpuAdmission137 = scope.submit([&]()noexcept{ ++callbacks; }, caller);
    ASSERT_TRUE(cpuAdmission137);
    if(cpuAdmission137)
        ASSERT_TRUE(cpuAdmission137->valid());
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
    const auto cpuAdmission138 = scheduler.submit([&](){
        const auto cpuAdmission139 = scheduler.submit([&](){ scope.wait(); });
        ASSERT_TRUE(cpuAdmission139);
        if(cpuAdmission139)
            ASSERT_TRUE(cpuAdmission139->valid());
    }
    );
    ASSERT_TRUE(cpuAdmission138);
    const auto ancestor = *cpuAdmission138;
    ASSERT_TRUE(ancestor.valid());
    const auto cpuAdmission140 = scheduler.submit([]()noexcept{}, ancestor);
    ASSERT_TRUE(cpuAdmission140);
    const auto middle = *cpuAdmission140;
    ASSERT_TRUE(middle.valid());
    const auto cpuAdmission141 = scope.submit([]()noexcept{}, middle);
    ASSERT_TRUE(cpuAdmission141);
    if(cpuAdmission141)
        ASSERT_TRUE(cpuAdmission141->valid());
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
    const auto cpuAdmission142 = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(cpuAdmission142);
    const auto caller = *cpuAdmission142;
    ASSERT_TRUE(caller.valid());
    const auto cpuAdmission143 = scope.submit([&](){
        ++callbacks;
        const auto cpuAdmission144 = scope.submit([&]()noexcept{ ++callbacks; }, caller);
        ASSERT_TRUE(cpuAdmission144);
        if(cpuAdmission144)
            ASSERT_TRUE(cpuAdmission144->valid());
    }
    );
    ASSERT_TRUE(cpuAdmission143);
    if(cpuAdmission143)
        ASSERT_TRUE(cpuAdmission143->valid());
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
    const auto cpuAdmission145 = scheduler.submit([&](){ scope.wait(); });
    ASSERT_TRUE(cpuAdmission145);
    const auto caller = *cpuAdmission145;
    ASSERT_TRUE(caller.valid());
    const auto cpuAdmission146 = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(cpuAdmission146);
    const auto prerequisite = *cpuAdmission146;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuAdmission147 = scope.submit([&](){
        ++callbacks;
        const auto cpuAdmission148 = scheduler.submit([&]()noexcept{ ++callbacks; });
        ASSERT_TRUE(cpuAdmission148);
        if(cpuAdmission148)
            ASSERT_TRUE(cpuAdmission148->valid());
    }, prerequisite
    );
    ASSERT_TRUE(cpuAdmission147);
    if(cpuAdmission147)
        ASSERT_TRUE(cpuAdmission147->valid());
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
    const auto cpuAdmission149 = scheduler.submit([&](){ EXPECT_EQ(++sequence, s_ExpectedDualCount); });
    ASSERT_TRUE(cpuAdmission149);
    const auto prerequisite = *cpuAdmission149;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuAdmission150 = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuAdmission150);
    if(cpuAdmission150)
        ASSERT_TRUE(cpuAdmission150->valid());
    const auto cpuAdmission151 = scope.submit([&](){
        EXPECT_EQ(++sequence, 1u);
        const auto cpuAdmission152 = scope.submit([&](){ EXPECT_EQ(++sequence, 3u); }, prerequisite);
        ASSERT_TRUE(cpuAdmission152);
        if(cpuAdmission152)
            ASSERT_TRUE(cpuAdmission152->valid());
    }
    );
    ASSERT_TRUE(cpuAdmission151);
    if(cpuAdmission151)
        ASSERT_TRUE(cpuAdmission151->valid());
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
    const auto cpuAdmission153 = scheduler.submit([&]()noexcept{ ++callbacks; });
    ASSERT_TRUE(cpuAdmission153);
    const auto unrelated = *cpuAdmission153;
    ASSERT_TRUE(unrelated.valid());
    const auto cpuAdmission154 = scope.submit([&](){
        EXPECT_EQ(callbacks, 0u);
        scheduler.wait(unrelated);
        const auto cpuAdmission155 = scheduler.submit([&]()noexcept{ ++callbacks; });
        ASSERT_TRUE(cpuAdmission155);
        const auto child = *cpuAdmission155;
        ASSERT_TRUE(child.valid());
        EXPECT_EQ(child.index, unrelated.index);
        EXPECT_NE(child.generation, unrelated.generation);
    }
    );
    ASSERT_TRUE(cpuAdmission154);
    if(cpuAdmission154)
        ASSERT_TRUE(cpuAdmission154->valid());
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
    const auto cpuAdmission156 = scheduler.submit([&]()noexcept{ ++innerCallbacks; });
    ASSERT_TRUE(cpuAdmission156);
    const auto prerequisite = *cpuAdmission156;
    ASSERT_TRUE(prerequisite.valid());
    const auto cpuAdmission157 = inner.submit([&]()noexcept{ ++innerCallbacks; }, prerequisite);
    ASSERT_TRUE(cpuAdmission157);
    if(cpuAdmission157)
        ASSERT_TRUE(cpuAdmission157->valid());
    const auto cpuAdmission158 = scheduler.submit([&]()noexcept{ ++unrelatedCallbacks; });
    ASSERT_TRUE(cpuAdmission158);
    if(cpuAdmission158)
        ASSERT_TRUE(cpuAdmission158->valid());
    const auto cpuAdmission159 = outer.submit([&](){
        EXPECT_EQ(innerCallbacks, 0u);
        inner.wait();
        EXPECT_EQ(innerCallbacks, s_ExpectedDualCount);
    }
    );
    ASSERT_TRUE(cpuAdmission159);
    if(cpuAdmission159)
        ASSERT_TRUE(cpuAdmission159->valid());
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
    const auto cpuAdmission160 = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(1u);
        scopeA.wait();
    }
    );
    ASSERT_TRUE(cpuAdmission160);
    const auto waiterA = *cpuAdmission160;
    const auto cpuAdmission161 = scheduler.submit([&](){
        entered.fetch_add(1u, MemoryOrder::release);
        while(!start.load(MemoryOrder::acquire))
            SleepMS(1u);
        scopeB.wait();
    }
    );
    ASSERT_TRUE(cpuAdmission161);
    const auto waiterB = *cpuAdmission161;
    ASSERT_TRUE(waiterA.valid());
    ASSERT_TRUE(waiterB.valid());
    while(entered.load(MemoryOrder::acquire) != s_ExpectedDualCount)
        SleepMS(1u);
    for(u32 index = 0u; index < 32u; ++index){
        const auto cpuAdmission162 = scheduler.submit([]()noexcept{});
        ASSERT_TRUE(cpuAdmission162);
        const auto prerequisiteB = *cpuAdmission162;
        const auto cpuAdmission163 = scheduler.submit([]()noexcept{});
        ASSERT_TRUE(cpuAdmission163);
        const auto prerequisiteA = *cpuAdmission163;
        EXPECT_TRUE(prerequisiteB.valid());
        EXPECT_TRUE(prerequisiteA.valid());
        const auto cpuAdmission164 = scopeB.submit([&]()noexcept{ visitsB.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteB);
        EXPECT_TRUE(cpuAdmission164);
        if(cpuAdmission164)
            EXPECT_TRUE(cpuAdmission164->valid());
        const auto cpuAdmission165 = scopeA.submit([&]()noexcept{ visitsA.fetch_add(1u, MemoryOrder::relaxed); }, prerequisiteA);
        EXPECT_TRUE(cpuAdmission165);
        if(cpuAdmission165)
            EXPECT_TRUE(cpuAdmission165->valid());
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
        const auto cpuAdmission166 = scheduler.submit([&]()noexcept{ ++unrelated; }, options);
        ASSERT_TRUE(cpuAdmission166);
        if(cpuAdmission166)
            ASSERT_TRUE(cpuAdmission166->valid());
    }
    for(u32 index = 0u; index < s_ScopedTasks; ++index){
        CpuTaskOptions options;
        options.priority = static_cast<CpuTaskPriority::Enum>(index % s_Priorities);
        const auto cpuAdmission167 = scope.submit([&]()noexcept{ ++joined; }, options);
        ASSERT_TRUE(cpuAdmission167);
        if(cpuAdmission167)
            ASSERT_TRUE(cpuAdmission167->valid());
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
    const auto cpuAdmission168 = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuAdmission168);
    const auto anchor = *cpuAdmission168;
    ASSERT_TRUE(anchor.valid());
    const auto cpuAdmission169 = scope.submit([&](){
        EXPECT_EQ(unrelated, 0u);
        scheduler.wait(anchor);
        EXPECT_EQ(unrelated, 1u);
        ++joined;
    }
    );
    ASSERT_TRUE(cpuAdmission169);
    if(cpuAdmission169)
        ASSERT_TRUE(cpuAdmission169->valid());
    const auto cpuAdmission170 = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuAdmission170);
    if(cpuAdmission170)
        ASSERT_TRUE(cpuAdmission170->valid());
    const auto cpuAdmission171 = scope.submit([&]()noexcept{ ++joined; });
    ASSERT_TRUE(cpuAdmission171);
    if(cpuAdmission171)
        ASSERT_TRUE(cpuAdmission171->valid());
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
    const auto cpuAdmission172 = scheduler.submit([&](){
        waiterEntered.store(true, MemoryOrder::release);
        while(!startWaiter.load(MemoryOrder::acquire))
            SleepMS(1u);
        scope.wait();
    }
    );
    ASSERT_TRUE(cpuAdmission172);
    const auto waiter = *cpuAdmission172;
    ASSERT_TRUE(waiter.valid());
    while(!waiterEntered.load(MemoryOrder::acquire))
        SleepMS(1u);
    const auto cpuAdmission173 = scheduler.submit([&](){
        blockerEntered.store(true, MemoryOrder::release);
        while(!releaseBlocker.load(MemoryOrder::acquire))
            SleepMS(1u);
    }
    );
    ASSERT_TRUE(cpuAdmission173);
    if(cpuAdmission173)
        ASSERT_TRUE(cpuAdmission173->valid());
    while(!blockerEntered.load(MemoryOrder::acquire))
        SleepMS(1u);
    const auto cpuAdmission174 = scheduler.submit([&](){
        anchorEntered.store(true, MemoryOrder::release);
        while(!releaseAnchor.load(MemoryOrder::acquire))
            SleepMS(1u);
    }
    );
    ASSERT_TRUE(cpuAdmission174);
    if(cpuAdmission174)
        ASSERT_TRUE(cpuAdmission174->valid());
    const auto cpuAdmission175 = scope.submit([&](){
        releaseBlocker.store(true, MemoryOrder::release);
        while(!anchorEntered.load(MemoryOrder::acquire))
            SleepMS(1u);
        joined.fetch_add(1u, MemoryOrder::relaxed);
    }
    );
    ASSERT_TRUE(cpuAdmission175);
    if(cpuAdmission175)
        ASSERT_TRUE(cpuAdmission175->valid());
    const auto cpuAdmission176 = scheduler.submit([&]()noexcept{ unrelated.fetch_add(1u, MemoryOrder::relaxed); });
    ASSERT_TRUE(cpuAdmission176);
    if(cpuAdmission176)
        ASSERT_TRUE(cpuAdmission176->valid());
    const auto cpuAdmission177 = scope.submit([&](){
        EXPECT_EQ(unrelated.load(MemoryOrder::acquire), 0u);
        joined.fetch_add(1u, MemoryOrder::relaxed);
        releaseAnchor.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(cpuAdmission177);
    if(cpuAdmission177)
        ASSERT_TRUE(cpuAdmission177->valid());
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
    const auto cpuAdmission178 = scheduler.submit([&](){
        entered.store(true, MemoryOrder::release);
        while(!release.load(MemoryOrder::acquire))
            SleepMS(1u);
    }
    );
    ASSERT_TRUE(cpuAdmission178);
    const auto prerequisite = *cpuAdmission178;
    ASSERT_TRUE(prerequisite.valid());
    while(!entered.load(MemoryOrder::acquire))
        SleepMS(1u);
    CpuTaskOptions mainThread;
    mainThread.target = CpuTaskTarget::MainThread;
    u32 unrelated = 0u;
    u32 joined = 0u;
    const auto cpuAdmission179 = scheduler.submit([&]()noexcept{ ++unrelated; }, mainThread);
    ASSERT_TRUE(cpuAdmission179);
    if(cpuAdmission179)
        ASSERT_TRUE(cpuAdmission179->valid());
    const auto cpuAdmission180 = scope.submit([&]()noexcept{
        ++joined;
        release.store(true, MemoryOrder::release);
    }, mainThread
    );
    ASSERT_TRUE(cpuAdmission180);
    if(cpuAdmission180)
        ASSERT_TRUE(cpuAdmission180->valid());
    const auto cpuAdmission181 = scope.submit([&]()noexcept{ ++joined; }, mainThread, &prerequisite, 1u);
    ASSERT_TRUE(cpuAdmission181);
    if(cpuAdmission181)
        ASSERT_TRUE(cpuAdmission181->valid());
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
    const auto cpuAdmission182 = scheduler.submit([&]()noexcept{ ++prerequisites; });
    ASSERT_TRUE(cpuAdmission182);
    const auto root = *cpuAdmission182;
    ASSERT_TRUE(root.valid());
    const auto cpuAdmission183 = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(cpuAdmission183);
    const auto sibling = *cpuAdmission183;
    ASSERT_TRUE(sibling.valid());
    const auto cpuAdmission184 = scheduler.submit([&]()noexcept{ ++unrelated; }, sibling);
    ASSERT_TRUE(cpuAdmission184);
    if(cpuAdmission184)
        ASSERT_TRUE(cpuAdmission184->valid());
    const auto cpuAdmission185 = scheduler.submit([&]()noexcept{ ++prerequisites; }, root);
    ASSERT_TRUE(cpuAdmission185);
    const auto positive = *cpuAdmission185;
    ASSERT_TRUE(positive.valid());
    const auto cpuAdmission186 = scope.submit([&]()noexcept{ ++joined; }, positive);
    ASSERT_TRUE(cpuAdmission186);
    if(cpuAdmission186)
        ASSERT_TRUE(cpuAdmission186->valid());
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
    const auto cpuAdmission187 = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuAdmission187);
    const auto root = *cpuAdmission187;
    ASSERT_TRUE(root.valid());
    const auto cpuAdmission188 = scheduler.submit([&]()noexcept{ ++prerequisite; }, root);
    ASSERT_TRUE(cpuAdmission188);
    const auto sibling = *cpuAdmission188;
    ASSERT_TRUE(sibling.valid());
    const auto cpuAdmission189 = scheduler.submit([&]()noexcept{ ++unrelatedTail; }, sibling);
    ASSERT_TRUE(cpuAdmission189);
    if(cpuAdmission189)
        ASSERT_TRUE(cpuAdmission189->valid());
    const auto cpuAdmission190 = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(cpuAdmission190);
    const auto positive = *cpuAdmission190;
    ASSERT_TRUE(positive.valid());
    const auto cpuAdmission191 = scope.submit([&](){
        EXPECT_EQ(prerequisite, 0u);
        ++joined;
        const auto cpuAdmission192 = scope.submit([&]()noexcept{ ++joined; }, sibling);
        EXPECT_TRUE(cpuAdmission192);
        if(cpuAdmission192)
            EXPECT_TRUE(cpuAdmission192->valid());
    }, positive
    );
    ASSERT_TRUE(cpuAdmission191);
    if(cpuAdmission191)
        ASSERT_TRUE(cpuAdmission191->valid());
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
    const auto cpuAdmission193 = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuAdmission193);
    const auto root = *cpuAdmission193;
    ASSERT_TRUE(root.valid());
    const auto cpuAdmission194 = scheduler.submit([&]()noexcept{ ++unrelated; }, root);
    ASSERT_TRUE(cpuAdmission194);
    const auto sibling = *cpuAdmission194;
    ASSERT_TRUE(sibling.valid());
    const auto cpuAdmission195 = scheduler.submit([&]()noexcept{ ++unrelated; }, sibling);
    ASSERT_TRUE(cpuAdmission195);
    if(cpuAdmission195)
        ASSERT_TRUE(cpuAdmission195->valid());
    const auto cpuAdmission196 = scope.submit([&]()noexcept{ ++joined; }, root);
    ASSERT_TRUE(cpuAdmission196);
    if(cpuAdmission196)
        ASSERT_TRUE(cpuAdmission196->valid());
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
    const auto cpuAdmission197 = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuAdmission197);
    const auto root = *cpuAdmission197;
    ASSERT_TRUE(root.valid());
    const auto cpuAdmission198 = scheduler.submit([]()noexcept{}, root);
    ASSERT_TRUE(cpuAdmission198);
    const auto positive = *cpuAdmission198;
    ASSERT_TRUE(positive.valid());
    const auto cpuAdmission199 = scope.submit([&](){
        CpuTaskHandle recycled;
        JoiningThread producer([&](){
            const auto cpuAdmission200 = scheduler.submit([&](){ ++unrelated; });
            ASSERT_TRUE(cpuAdmission200);
            recycled =*cpuAdmission200;
         });
        producer.join();
        EXPECT_TRUE(recycled.valid());
        EXPECT_EQ(recycled.index, positive.index);
        EXPECT_NE(recycled.generation, positive.generation);
        ++joined;
        const auto cpuAdmission201 = scope.submit([&]()noexcept{ ++joined; });
        EXPECT_TRUE(cpuAdmission201);
        if(cpuAdmission201)
            EXPECT_TRUE(cpuAdmission201->valid());
    }, positive
    );
    ASSERT_TRUE(cpuAdmission199);
    if(cpuAdmission199)
        ASSERT_TRUE(cpuAdmission199->valid());
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
    const auto cpuAdmission202 = scheduler.submit([&]()noexcept{ ++unrelated; });
    ASSERT_TRUE(cpuAdmission202);
    if(cpuAdmission202)
        ASSERT_TRUE(cpuAdmission202->valid());
    const auto cpuAdmission203 = scope.submit([&](){
        for(u32 child = 0u; child < s_Children; ++child){
            const auto cpuAdmission204 = scheduler.submit([&]()noexcept{ ++children; });
            EXPECT_TRUE(cpuAdmission204);
            if(cpuAdmission204)
                EXPECT_TRUE(cpuAdmission204->valid());
        }
    }
    );
    ASSERT_TRUE(cpuAdmission203);
    if(cpuAdmission203)
        ASSERT_TRUE(cpuAdmission203->valid());
    scope.wait();
    EXPECT_EQ(children, s_Children);
    EXPECT_EQ(unrelated, 0u);
    scheduler.wait();
    EXPECT_EQ(unrelated, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

