// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/cpu/scheduler.h>

#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#endif
#if defined(NWB_PLATFORM_LINUX)
#include <sched.h>
#include <unistd.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;

inline constexpr u32 s_GateTimeoutMS = 4000u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


class DeadlineGuard final{
public:
    explicit DeadlineGuard(bool deathTest = false)
        : m_watchdog([deathTest](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS){
                    // A timeout must fail a death test instead of being mistaken for the expected invariant rejection.
                    if(deathTest){
#if defined(NWB_PLATFORM_WINDOWS)
                        ExitProcess(0u);
#elif defined(NWB_PLATFORM_LINUX)
                        ::_exit(0);
#endif
                    }
                    TerminateInvariant();
                }
                SleepMS(1u);
            }
        })
    {}
    ~DeadlineGuard() = default;


private:
    JoiningThread m_watchdog;
};


[[noreturn]] void ExitTestProcess(u32 code)noexcept{
#if defined(NWB_PLATFORM_WINDOWS)
    ExitProcess(code);
#elif defined(NWB_PLATFORM_LINUX)
    ::_exit(static_cast<int>(code));
#else
    static_cast<void>(code);
    TerminateInvariant();
#endif
}


[[nodiscard]] bool WaitUntil(const Atomic<bool>& condition){
    const Timer begin = TimerNow();
    while(!condition.load(MemoryOrder::acquire)){
        if(DurationInMS<u64>(TimerNow(), begin) >= s_GateTimeoutMS)
            return false;
        SleepMS(1u);
    }
    return true;
}


struct TaskGate{
    Atomic<bool> entered{ false };
    Atomic<bool> released{ false };
    Atomic<bool> expired{ false };


    void block(){
        entered.store(true, MemoryOrder::release);
        if(!WaitUntil(released))
            expired.store(true, MemoryOrder::release);
    }

    void open()noexcept{ released.store(true, MemoryOrder::release); }
};


struct LifetimeProbe{
    Atomic<u32>* retirements;
    u32 value;


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


[[nodiscard]] CpuTaskSchedulerConfig HomogeneousWorkers(u32 count){
    CpuTaskSchedulerConfig config;
    config.workerCount = count;
    config.heterogeneous = false;
    return config;
}


[[nodiscard]] CpuAffinity::Enum ActualProcessorAffinity(const InteropVector<CpuWorkerPlacement>& placements){
    u32 group = 0u;
    u32 processor = CpuWorkerPlacement::s_InvalidProcessor;
#if defined(NWB_PLATFORM_WINDOWS)
    PROCESSOR_NUMBER location{};
    GetCurrentProcessorNumberEx(&location);
    group = location.Group;
    processor = location.Number;
#elif defined(NWB_PLATFORM_LINUX)
    const int location = ::sched_getcpu();
    if(location >= 0)
        processor = static_cast<u32>(location);
#endif
    const auto found = FindIf(placements.begin(), placements.end(), [group, processor](const CpuWorkerPlacement& placement){
        return placement.processorGroup == group && placement.logicalProcessorIndex == processor;
    });
    return found == placements.end() ? CpuAffinity::Any : found->affinity;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, DependencyFanInWaitsForEveryPredecessorIncludingDuplicateEdges){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate rootGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(2u));
    ScopeExit releaseRoot([&]()noexcept{ rootGate.open(); });
    Atomic<u32> completedBits{ 0u };
    Atomic<u32> observedBits{ 0u };
    const auto root = scheduler.submit([&](){ rootGate.block(); });
    EXPECT_TRUE(WaitUntil(rootGate.entered));
    CpuTaskHandle dependencies[6u];
    for(u32 index = 0u; index < 4u; ++index){
        dependencies[index] = scheduler.submit([&completedBits, index](){
            completedBits.fetch_or(1u << index, MemoryOrder::release);
        }, root);
        EXPECT_TRUE(dependencies[index].valid());
    }
    dependencies[4u] = dependencies[1u];
    dependencies[5u] = {};
    const auto join = scheduler.submit([&](){
        observedBits.store(completedBits.load(MemoryOrder::acquire), MemoryOrder::release);
    }, {}, dependencies, 6u);
    EXPECT_TRUE(join.valid());
    EXPECT_FALSE(scheduler.isComplete(join));
    rootGate.open();
    scheduler.wait(join);
    EXPECT_EQ(observedBits.load(), 15u);
    EXPECT_FALSE(rootGate.expired.load());
    EXPECT_EQ(scheduler.statistics().completedTasks, 6u);
}


TEST(CpuTaskSchedulerTests, ForeignDependenciesAndForeignWaitsAreRejectedWithoutPublishingWork){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler first(0u);
    CpuTaskScheduler second(0u);
    const auto foreign = first.submit([](){});
    u32 invoked = 0u;
    const auto rejected = second.submit([&](){ ++invoked; }, foreign);
    EXPECT_FALSE(rejected.valid());
    EXPECT_EQ(second.statistics().outstandingTasks, 0u);
    EXPECT_THROW(second.wait(foreign), RuntimeException);
    first.wait(foreign);
    EXPECT_FALSE(second.submit([&](){ ++invoked; }, foreign).valid());
    EXPECT_EQ(invoked, 0u);
}


TEST(CpuTaskSchedulerTests, RetiredHandlesStayCompleteWhenNodeStorageIsReused){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    const auto retired = scheduler.submit([](){});
    scheduler.wait(retired);
    u32 invoked = 0u;
    const auto pending = scheduler.submit([&](){ ++invoked; });
    EXPECT_TRUE(scheduler.isComplete(retired));
    EXPECT_FALSE(scheduler.isComplete(pending));
    scheduler.wait(retired);
    EXPECT_EQ(invoked, 0u);
    scheduler.wait(pending);
    EXPECT_EQ(invoked, 1u);
}


TEST(CpuTaskSchedulerTests, ParentCompletionRetainsAncestorCapturesUntilGrandchildrenAndRetirementFinish){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate grandchildGate;
    Atomic<u32> retirements{ 0u };
    Atomic<u32> total{ 0u };
    Atomic<u32> retirementsSeenByDependent{ 0u };
    Atomic<bool> rejected{ false };
    CpuTaskScheduler scheduler(HomogeneousWorkers(2u));
    ScopeExit releaseGrandchild([&]()noexcept{ grandchildGate.open(); });
    const auto parent = scheduler.submit([
        &scheduler, &grandchildGate, &retirements, &total, &rejected, parentCapture = LifetimeProbe(retirements, 11u)
    ]() mutable{
        const auto child = scheduler.submit([
            &scheduler, &grandchildGate, &retirements, &total, &rejected, &parentCapture,
            childCapture = LifetimeProbe(retirements, 13u)
        ]() mutable{
            const auto grandchild = scheduler.submit([
                &grandchildGate, &total, &parentCapture, &childCapture, grandchildCapture = LifetimeProbe(retirements, 17u)
            ](){
                grandchildGate.block();
                total.store(parentCapture.value + childCapture.value + grandchildCapture.value, MemoryOrder::release);
            });
            if(!grandchild.valid())
                rejected.store(true, MemoryOrder::release);
        });
        if(!child.valid())
            rejected.store(true, MemoryOrder::release);
    });
    const auto dependent = scheduler.submit([&](){
        retirementsSeenByDependent.store(retirements.load(MemoryOrder::acquire), MemoryOrder::release);
    }, parent);
    EXPECT_TRUE(WaitUntil(grandchildGate.entered));
    EXPECT_FALSE(scheduler.isComplete(parent));
    EXPECT_FALSE(scheduler.isComplete(dependent));
    EXPECT_EQ(retirements.load(), 0u);
    grandchildGate.open();
    scheduler.wait(dependent);
    EXPECT_FALSE(rejected.load());
    EXPECT_FALSE(grandchildGate.expired.load());
    EXPECT_EQ(total.load(), 41u);
    EXPECT_EQ(retirements.load(), 3u);
    EXPECT_EQ(retirementsSeenByDependent.load(), 3u);
}


TEST(CpuTaskSchedulerTests, ScopeJoinDoesNotWaitForAnUnrelatedRunningScope){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate unrelatedGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(2u));
    CpuTaskScope unrelated(scheduler);
    CpuTaskScope owned(scheduler);
    ScopeExit releaseUnrelated([&]()noexcept{ unrelatedGate.open(); });
    const auto unrelatedTask = unrelated.submit([&](){ unrelatedGate.block(); });
    EXPECT_TRUE(WaitUntil(unrelatedGate.entered));
    Atomic<bool> ownedFinished{ false };
    EXPECT_TRUE(owned.submit([&](){ ownedFinished.store(true, MemoryOrder::release); }).valid());
    owned.wait();
    EXPECT_TRUE(ownedFinished.load());
    EXPECT_FALSE(scheduler.isComplete(unrelatedTask));
    EXPECT_FALSE(unrelatedGate.expired.load());
    unrelatedGate.open();
    unrelated.wait();
}


TEST(CpuTaskSchedulerTests, ScopeDestructionIncludesChildrenSubmittedDirectlyToTheScheduler){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    Atomic<u32> invoked{ 0u };
    Atomic<bool> rejected{ false };
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    {
        CpuTaskScope scope(scheduler);
        EXPECT_TRUE(scope.submit([&](){
            if(!scheduler.submit([&invoked, capture = LifetimeProbe(retired, 23u)](){
                invoked.store(capture.value, MemoryOrder::release);
            }).valid())
                rejected.store(true, MemoryOrder::release);
        }).valid());
    }
    EXPECT_FALSE(rejected.load());
    EXPECT_EQ(invoked.load(), 23u);
    EXPECT_EQ(retired.load(), 1u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, OneWorkerCompletesNestedParallelRangesThroughCooperativeExecution){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    Atomic<u32> visited[35u]{};
    const auto root = scheduler.submit([&](){
        scheduler.parallelFor(0u, 5u, 1u, [&](const usize outer){
            scheduler.parallelFor(0u, 7u, 1u, [&](const usize inner){
                visited[outer * 7u + inner].fetch_add(1u, MemoryOrder::relaxed);
            });
        });
    });
    scheduler.wait(root);
    for(const auto& count : visited)
        EXPECT_EQ(count.load(), 1u);
    EXPECT_GT(scheduler.statistics().cooperativeTasks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, MainThreadTasksSubmittedByWorkersExecuteOnlyOnTheSchedulerOwner){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    const ThreadId owner = QueryCurrentThreadId();
    Atomic<bool> published{ false };
    Atomic<bool> rejected{ false };
    Atomic<u32> invocations{ 0u };
    ThreadId actual;
    usize workerIndex = Limit<usize>::s_Max;
    const auto parent = scheduler.submit([&](){
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        if(!scheduler.submit([&](){
            actual = QueryCurrentThreadId();
            workerIndex = scheduler.currentWorkerIndex();
            invocations.fetch_add(1u, MemoryOrder::release);
        }, mainOptions).valid())
            rejected.store(true, MemoryOrder::release);
        published.store(true, MemoryOrder::release);
    });
    EXPECT_TRUE(WaitUntil(published));
    EXPECT_EQ(invocations.load(), 0u);
    scheduler.wait(parent);
    EXPECT_FALSE(rejected.load());
    EXPECT_EQ(actual, owner);
    EXPECT_EQ(workerIndex, 0u);
    EXPECT_EQ(invocations.load(), 1u);
}


TEST(CpuTaskSchedulerTests, ZeroWorkersMakeProgressAcrossChildrenDependenciesAndMainThreadTasks){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    const ThreadId owner = QueryCurrentThreadId();
    u32 value = 0u;
    bool rejected = false;
    ThreadId actual;
    const auto parent = scheduler.submit([&](){
        const auto child = scheduler.submit([&](){ value = 17u; });
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        rejected = !scheduler.submit([&](){ value *= 3u; actual = QueryCurrentThreadId(); }, mainOptions, &child, 1u).valid();
    });
    scheduler.wait(parent);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(value, 51u);
    EXPECT_EQ(actual, owner);
    EXPECT_EQ(scheduler.workerThreadCount(), 0u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 3u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, QueuedPrioritiesRunCriticalThenNormalThenBackgroundAfterOccupiedWorkerReleases){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate occupied;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    ScopeExit releaseWorker([&]()noexcept{ occupied.open(); });
    EXPECT_TRUE(scheduler.submit([&](){ occupied.block(); }).valid());
    EXPECT_TRUE(WaitUntil(occupied.entered));
    Atomic<u32> cursor{ 0u };
    u32 order[3u]{};
    const CpuTaskPriority::Enum priorities[3u] = {
        CpuTaskPriority::Background, CpuTaskPriority::Normal, CpuTaskPriority::Critical
    };
    for(u32 index = 0u; index < 3u; ++index){
        CpuTaskOptions options;
        options.priority = priorities[index];
        EXPECT_TRUE(scheduler.submit([&, index](){ order[cursor.fetch_add(1u)] = index; }, options).valid());
    }
    occupied.open();
    scheduler.wait();
    EXPECT_FALSE(occupied.expired.load());
    EXPECT_EQ(cursor.load(), 3u);
    EXPECT_EQ(order[0u], 2u);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[2u], 0u);
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsQueuedBodiesAndTheirDependentsAndRetiresCaptures){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invoked = 0u;
    const auto predecessor = scope.submit([&invoked, capture = LifetimeProbe(retired, 1u)](){ invoked += capture.value; });
    const auto dependent = scheduler.submit([&invoked, capture = LifetimeProbe(retired, 2u)](){ invoked += capture.value; }, predecessor);
    scope.cancel();
    EXPECT_FALSE(scope.submit([&](){ ++invoked; }).valid());
    scheduler.wait(dependent);
    scope.wait();
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(retired.load(), 2u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 2u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 0u);
}


TEST(CpuTaskSchedulerTests, CancelingAnActiveParentPreventsFutureDescendantsAndContinuations){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate parentGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
    CpuTaskScope scope(scheduler);
    ScopeExit releaseParent([&]()noexcept{ parentGate.open(); });
    Atomic<u32> childInvocations{ 0u };
    Atomic<u32> continuationInvocations{ 0u };
    Atomic<bool> childRejected{ false };
    const auto parent = scope.submit([&](){
        parentGate.block();
        if(!scheduler.submit([&](){ childInvocations.fetch_add(1u); }).valid())
            childRejected.store(true, MemoryOrder::release);
    });
    EXPECT_TRUE(WaitUntil(parentGate.entered));
    const auto continuation = scheduler.submit([&](){ continuationInvocations.fetch_add(1u); }, parent);
    scope.cancel();
    parentGate.open();
    scheduler.wait(continuation);
    scope.wait();
    EXPECT_FALSE(parentGate.expired.load());
    EXPECT_EQ(childInvocations.load(), 0u);
    EXPECT_EQ(continuationInvocations.load(), 0u);
    EXPECT_GE(scheduler.statistics().canceledTasks, 2u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, StatisticsAccountForQueuedCompletedAndCanceledTasks){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskHandle predecessors[2u] = { scheduler.submit([](){}), scheduler.submit([](){}) };
    const auto joined = scheduler.submit([](){}, {}, predecessors, 2u);
    const auto queued = scheduler.statistics();
    EXPECT_EQ(queued.outstandingTasks, 3u);
    EXPECT_EQ(queued.peakOutstandingTasks, 3u);
    scheduler.wait(joined);
    {
        CpuTaskScope canceled(scheduler);
        EXPECT_TRUE(canceled.submit([](){}).valid());
        EXPECT_TRUE(canceled.submit([](){}).valid());
        canceled.cancel();
    }
    const auto finished = scheduler.statistics();
    EXPECT_EQ(finished.completedTasks, 3u);
    EXPECT_EQ(finished.canceledTasks, 2u);
    EXPECT_EQ(finished.outstandingTasks, 0u);
    EXPECT_EQ(finished.peakOutstandingTasks, 3u);
    EXPECT_EQ(finished.performanceTasks + finished.efficiencyTasks + finished.unclassifiedTasks, 3u);
    EXPECT_EQ(finished.performanceWorkers + finished.efficiencyWorkers + finished.unclassifiedWorkers, 0u);
}


TEST(CpuTaskSchedulerTests, IsolatedHeavyAndLightTasksUseTheirMatchingPhysicalCapacityClasses){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    InteropVector<CpuWorkerPlacement> topology;
    ASSERT_TRUE(QueryCpuWorkerPlacements(topology));
    CpuTaskScheduler scheduler(2u);
    const auto workers = scheduler.statistics();
    if(workers.performanceWorkers == 0u || workers.efficiencyWorkers == 0u)
        GTEST_SKIP() << "The permitted CPUs do not provide both capacity classes.";
    for(const CpuTaskCost::Enum cost : { CpuTaskCost::Heavy, CpuTaskCost::Light }){
        CpuTaskOptions options;
        options.cost = cost;
        CpuAffinity::Enum reported = CpuAffinity::Any;
        CpuAffinity::Enum physical = CpuAffinity::Any;
        const auto task = scheduler.submit([&](){
            reported = scheduler.currentWorkerAffinity();
            physical = ActualProcessorAffinity(topology);
        }, options);
        scheduler.wait(task);
        const CpuAffinity::Enum expected = cost == CpuTaskCost::Heavy ? CpuAffinity::Performance : CpuAffinity::Efficiency;
        EXPECT_EQ(reported, expected);
        EXPECT_EQ(physical, expected);
    }
    EXPECT_EQ(scheduler.statistics().placementFailures, 0u);
}


TEST(CpuTaskSchedulerTests, HeavyWorkUsesTheOtherClassWhileItsPreferredWorkerIsOccupied){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate preferredGate;
    TaskGate spillGate;
    CpuTaskScheduler scheduler(2u);
    ScopeExit releaseWorkers([&]()noexcept{ preferredGate.open(); spillGate.open(); });
    const auto workers = scheduler.statistics();
    if(workers.performanceWorkers == 0u || workers.efficiencyWorkers == 0u)
        GTEST_SKIP() << "The permitted CPUs do not provide both capacity classes.";
    CpuAffinity::Enum preferredAffinity = CpuAffinity::Any;
    CpuAffinity::Enum spillAffinity = CpuAffinity::Any;
    EXPECT_TRUE(scheduler.submit([&](){
        preferredAffinity = scheduler.currentWorkerAffinity();
        preferredGate.block();
    }).valid());
    EXPECT_TRUE(WaitUntil(preferredGate.entered));
    EXPECT_TRUE(scheduler.submit([&](){
        spillAffinity = scheduler.currentWorkerAffinity();
        spillGate.block();
    }).valid());
    EXPECT_TRUE(WaitUntil(spillGate.entered));
    preferredGate.open();
    spillGate.open();
    scheduler.wait();
    EXPECT_FALSE(preferredGate.expired.load());
    EXPECT_FALSE(spillGate.expired.load());
    EXPECT_EQ(preferredAffinity, CpuAffinity::Performance);
    EXPECT_EQ(spillAffinity, CpuAffinity::Efficiency);
    EXPECT_GT(scheduler.statistics().performanceTasks, 0u);
    EXPECT_GT(scheduler.statistics().efficiencyTasks, 0u);
}


TEST(CpuTaskSchedulerTests, WorkerSelfWaitIsRejectedInsteadOfDeadlocking){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle self;
        self = scheduler.submit([&](){ start.block(); scheduler.wait(self); });
        start.open();
        scheduler.wait();
    }, "");
}


TEST(CpuTaskSchedulerTests, DescendantWaitForAnAncestorIsRejectedInsteadOfDeadlocking){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle parent;
        parent = scheduler.submit([&](){
            start.block();
            const auto child = scheduler.submit([&](){ scheduler.wait(parent); });
            EXPECT_TRUE(child.valid());
        });
        start.open();
        scheduler.wait();
    }, "");
}


TEST(CpuTaskSchedulerTests, JoiningTheScopeThatContainsTheCurrentTaskIsRejected){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskScope scope(scheduler);
        EXPECT_TRUE(scope.submit([&](){ scope.wait(); }).valid());
        scheduler.wait();
    }, "");
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsBothDirectParallelRangeOverloads){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invocations = 0u;
    scope.cancel();
    scope.parallelFor(0u, 17u, [&](usize){ ++invocations; });
    scope.parallelFor(4u, 25u, 3u, [&](usize){ ++invocations; });
    scope.wait();
    EXPECT_EQ(invocations, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, DirectParallelRangeChildrenParticipateInTheReceivingScopesCancellation){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invocations = 0u;
    scope.parallelFor(0u, 64u, 1u, [&](usize index){
        ++invocations;
        if(index == 0u)
            scope.cancel();
    });
    scope.wait();
    EXPECT_GT(invocations, 0u);
    EXPECT_LT(invocations, 64u);
    EXPECT_GT(scheduler.statistics().canceledTasks, 0u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, LateDependenciesRetainCanceledResultsAfterRepeatedNodeReuse){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope canceledScope(scheduler);
    u32 canceledInvocations = 0u;
    u32 unrelatedInvocations = 0u;
    const auto canceled = canceledScope.submit([&](){ ++canceledInvocations; });
    canceledScope.cancel();
    canceledScope.wait();
    for(u32 iteration = 0u; iteration < 16u; ++iteration){
        const auto unrelated = scheduler.submit([&](){ ++unrelatedInvocations; });
        scheduler.wait(unrelated);
    }
    const auto lateDependent = scheduler.submit([&](){ ++canceledInvocations; }, canceled);
    ASSERT_TRUE(lateDependent.valid());
    scheduler.wait(lateDependent);
    const auto laterDependent = scheduler.submit([&](){ ++canceledInvocations; }, lateDependent);
    ASSERT_TRUE(laterDependent.valid());
    scheduler.wait(laterDependent);
    EXPECT_EQ(canceledInvocations, 0u);
    EXPECT_EQ(unrelatedInvocations, 16u);
    EXPECT_EQ(scheduler.statistics().canceledTasks, 3u);
    EXPECT_EQ(scheduler.statistics().completedTasks, 16u);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, ChildCannotDependDirectlyOnItsStructuredParent){
    EXPECT_EXIT({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline;
        CpuTaskScheduler scheduler(0u);
        CpuTaskHandle parent;
        bool rejected = false;
        parent = scheduler.submit([&](){
            const auto child = scheduler.submit([](){}, parent);
            rejected = !child.valid();
        });
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(parent);
        ExitTestProcess(scheduler.statistics().outstandingTasks == 0u ? 0u : 2u);
    }, testing::ExitedWithCode(0), "");
}


TEST(CpuTaskSchedulerTests, ChildCannotDependOnATransitiveDependentOfItsStructuredParent){
    EXPECT_EXIT({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline;
        CpuTaskScheduler scheduler(0u);
        CpuTaskHandle dependent;
        bool rejected = false;
        u32 continuations = 0u;
        const auto parent = scheduler.submit([&](){
            const auto child = scheduler.submit([](){}, dependent);
            rejected = !child.valid();
        });
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth)
            dependent = scheduler.submit([&](){ ++continuations; }, dependent);
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(dependent);
        ExitTestProcess(continuations == 3u && scheduler.statistics().outstandingTasks == 0u ? 0u : 2u);
    }, testing::ExitedWithCode(0), "");
}


TEST(CpuTaskSchedulerTests, DescendantOnAnotherWorkerCannotJoinItsAncestorsScope){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate parentGate;
        CpuTaskScheduler scheduler(HomogeneousWorkers(2u));
        CpuTaskScope scope(scheduler);
        Atomic<usize> parentWorker{ 0u };
        EXPECT_TRUE(scope.submit([&](){
            parentWorker.store(scheduler.currentWorkerIndex(), MemoryOrder::release);
            EXPECT_TRUE(scheduler.submit([&](){
                if(
                    !WaitUntil(parentGate.entered)
                    || scheduler.currentWorkerIndex() == parentWorker.load(MemoryOrder::acquire)
                )
                    ExitTestProcess(0u);
                scope.wait();
            }).valid());
            parentGate.block();
        }).valid());
        scheduler.wait();
    }, "");
}


TEST(CpuTaskSchedulerTests, WorkerCannotWaitForAnIndependentlySubmittedDependent){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto running = scheduler.submit([&](){ start.block(); scheduler.wait(dependent); });
        dependent = scheduler.submit([](){}, running);
        start.open();
        scheduler.wait();
    }, "");
}


TEST(CpuTaskSchedulerTests, DescendantCannotWaitForTransitiveDependentsOfItsAncestor){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto parent = scheduler.submit([&](){
            start.block();
            EXPECT_TRUE(scheduler.submit([&](){ scheduler.wait(dependent); }).valid());
        });
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth)
            dependent = scheduler.submit([](){}, dependent);
        start.open();
        scheduler.wait();
    }, "");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

