// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "process_exit.h"
#include "task_wait.h"

#include <core/task/cpu/scheduler.h>

#include <global/platform.h>
#include <global/termination.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;
constexpr u32 s_ThirdElementIndex = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB::Core;
using CpuTaskTestSupport::ExitTestProcess;

inline constexpr u32 s_GateTimeoutMS = 4000u;
inline constexpr u32 s_TestTimeoutMS = 15000u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeadlineGuard final{
public:
    explicit DeadlineGuard(bool deathTest = false)
        : m_watchdog([deathTest](const StopToken& stop){
            const Timer begin = TimerNow();
            while(!stop.stop_requested()){
                if(DurationInMS<u64>(TimerNow(), begin) >= s_TestTimeoutMS){
                    // A timeout must fail a death test instead of being mistaken for the expected invariant rejection.
                    if(deathTest)
                        ExitTestProcess(0u);
                    TerminateInvariant();
                }
                SleepMS(1u);
            }
        }
        )
    {}
    ~DeadlineGuard() = default;


private:
    JoiningThread m_watchdog;
};


struct TaskGate{
    Atomic<bool> entered{ false };
    Atomic<bool> released{ false };
    Atomic<bool> expired{ false };


public:
    void block(){
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


[[nodiscard]] CpuTaskSchedulerConfig HomogeneousWorkers(u32 count)noexcept{
    CpuTaskSchedulerConfig config;
    config.workerCount = count;
    config.heterogeneous = false;
    return config;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(CpuTaskSchedulerTests, DependencyFanInWaitsForEveryPredecessorIncludingDuplicateEdges){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate rootGate;
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    ScopeExit releaseRoot([&]()noexcept{ rootGate.open(); });
    Atomic<u32> completedBits{ 0u };
    Atomic<u32> observedBits{ 0u };
    const auto cpuAdmission81 = scheduler.submit([&](){ rootGate.block(); });
    ASSERT_TRUE(cpuAdmission81);
    const auto root = *cpuAdmission81;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(rootGate.entered, s_GateTimeoutMS));
    CpuTaskHandle dependencies[6u];
    for(u32 index = 0u; index < 4u; ++index){
        const auto cpuAdmission82 = scheduler.submit([&completedBits, index]()noexcept{
            completedBits.fetch_or(1u << index, MemoryOrder::release);
        }, root
        );
        ASSERT_TRUE(cpuAdmission82);
        dependencies[index] = *cpuAdmission82;
        EXPECT_TRUE(dependencies[index].valid());
    }
    dependencies[4u] = dependencies[1u];
    dependencies[5u] = {};
    const auto cpuAdmission83 = scheduler.submit([&]()noexcept{
        observedBits.store(completedBits.load(MemoryOrder::acquire), MemoryOrder::release);
    }, {}, dependencies, 6u
    );
    ASSERT_TRUE(cpuAdmission83);
    const auto join = *cpuAdmission83;
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
    const auto cpuAdmission84 = first.submit([]()noexcept{});
    ASSERT_TRUE(cpuAdmission84);
    const auto foreign = *cpuAdmission84;
    u32 invoked = 0u;
    const auto rejected = second.submit([&]()noexcept{ ++invoked; }, foreign);
    EXPECT_FALSE(rejected);
    EXPECT_EQ(second.statistics().outstandingTasks, 0u);
    EXPECT_THROW(second.wait(foreign), RuntimeException);
    first.wait(foreign);
    const auto cpuAdmission86 = second.submit([&]()noexcept{ ++invoked; }, foreign);
    EXPECT_FALSE(cpuAdmission86);
    EXPECT_EQ(invoked, 0u);
}


TEST(CpuTaskSchedulerTests, RetiredHandlesStayCompleteWhenNodeStorageIsReused){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    const auto cpuAdmission87 = scheduler.submit([]()noexcept{});
    ASSERT_TRUE(cpuAdmission87);
    const auto retired = *cpuAdmission87;
    scheduler.wait(retired);
    u32 invoked = 0u;
    const auto cpuAdmission88 = scheduler.submit([&]()noexcept{ ++invoked; });
    ASSERT_TRUE(cpuAdmission88);
    const auto pending = *cpuAdmission88;
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
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    ScopeExit releaseGrandchild([&]()noexcept{ grandchildGate.open(); });
    const auto cpuAdmission89 = scheduler.submit([&scheduler, &grandchildGate, &retirements, &total, &rejected, parentCapture = LifetimeProbe(retirements, 11u)]() mutable{
        const auto cpuAdmission90 = scheduler.submit([&scheduler, &grandchildGate, &retirements, &total, &rejected, &parentCapture, childCapture = LifetimeProbe(retirements, 13u)]() mutable{
            const auto cpuAdmission91 = scheduler.submit([&grandchildGate, &total, &parentCapture, &childCapture, grandchildCapture = LifetimeProbe(retirements, 17u)](){
                grandchildGate.block();
                total.store(parentCapture.value + childCapture.value + grandchildCapture.value, MemoryOrder::release);
            }
            );
            ASSERT_TRUE(cpuAdmission91);
            const auto grandchild = *cpuAdmission91;
            if(!grandchild.valid())
                rejected.store(true, MemoryOrder::release);
        }
        );
        ASSERT_TRUE(cpuAdmission90);
        const auto child = *cpuAdmission90;
        if(!child.valid())
            rejected.store(true, MemoryOrder::release); }
    );
    ASSERT_TRUE(cpuAdmission89);
    const auto parent = *cpuAdmission89;
    const auto cpuAdmission92 = scheduler.submit([&]()noexcept{
        retirementsSeenByDependent.store(retirements.load(MemoryOrder::acquire), MemoryOrder::release);
    }, parent
    );
    ASSERT_TRUE(cpuAdmission92);
    const auto dependent = *cpuAdmission92;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(grandchildGate.entered, s_GateTimeoutMS));
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
    CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
    CpuTaskScope unrelated(scheduler);
    CpuTaskScope owned(scheduler);
    ScopeExit releaseUnrelated([&]()noexcept{ unrelatedGate.open(); });
    const auto cpuAdmission93 = unrelated.submit([&](){ unrelatedGate.block(); });
    ASSERT_TRUE(cpuAdmission93);
    const auto unrelatedTask = *cpuAdmission93;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(unrelatedGate.entered, s_GateTimeoutMS));
    Atomic<bool> ownedFinished{ false };
    const auto cpuAdmission94 = owned.submit([&]()noexcept{ ownedFinished.store(true, MemoryOrder::release); });
    EXPECT_TRUE(cpuAdmission94);
    if(cpuAdmission94)
        EXPECT_TRUE(cpuAdmission94->valid());
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
        const auto cpuAdmission95 = scope.submit([&](){
            const auto cpuAdmission96 = scheduler.submit([&invoked, capture = LifetimeProbe(retired, 23u)](){
                invoked.store(capture.value, MemoryOrder::release);
            }
            );
            if(!cpuAdmission96 || !cpuAdmission96->valid())
                rejected.store(true, MemoryOrder::release);
        }
        );
        EXPECT_TRUE(cpuAdmission95);
        if(cpuAdmission95)
            EXPECT_TRUE(cpuAdmission95->valid());
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
    const auto cpuAdmission97 = scheduler.submit([&](){
        EXPECT_TRUE((scheduler.parallelFor(0u, 5u, 1u, [&](const usize outer){
            EXPECT_TRUE((scheduler.parallelFor(0u, 7u, 1u, [&](const usize inner)noexcept{
                visited[outer * 7u + inner].fetch_add(1u, MemoryOrder::relaxed);
            }
            )));
        }
        )));
    }
    );
    ASSERT_TRUE(cpuAdmission97);
    const auto root = *cpuAdmission97;
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
    const auto cpuAdmission98 = scheduler.submit([&](){
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        const auto cpuAdmission99 = scheduler.submit([&](){
            actual = QueryCurrentThreadId();
            workerIndex = scheduler.currentWorkerIndex();
            invocations.fetch_add(1u, MemoryOrder::release);
        }, mainOptions
        );
        if(!cpuAdmission99 || !cpuAdmission99->valid())
            rejected.store(true, MemoryOrder::release);
        published.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(cpuAdmission98);
    const auto parent = *cpuAdmission98;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(published, s_GateTimeoutMS));
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
    const auto cpuAdmission100 = scheduler.submit([&](){
        const auto cpuAdmission101 = scheduler.submit([&]()noexcept{ value = 17u; });
        ASSERT_TRUE(cpuAdmission101);
        const auto child = *cpuAdmission101;
        CpuTaskOptions mainOptions;
        mainOptions.target = CpuTaskTarget::MainThread;
        const auto cpuAdmission102 = scheduler.submit([&](){ value *= 3u; actual = QueryCurrentThreadId(); }, mainOptions, &child, 1u);
        rejected = !cpuAdmission102 || !cpuAdmission102->valid();
    }
    );
    ASSERT_TRUE(cpuAdmission100);
    const auto parent = *cpuAdmission100;
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
    const auto cpuAdmission103 = scheduler.submit([&](){ occupied.block(); });
    EXPECT_TRUE(cpuAdmission103);
    if(cpuAdmission103)
        EXPECT_TRUE(cpuAdmission103->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(occupied.entered, s_GateTimeoutMS));
    Atomic<u32> cursor{ 0u };
    u32 order[3u]{};
    const CpuTaskPriority::Enum priorities[3u] = { CpuTaskPriority::Background, CpuTaskPriority::Normal, CpuTaskPriority::Critical };
    for(u32 index = 0u; index < 3u; ++index){
        CpuTaskOptions options;
        options.priority = priorities[index];
        const auto cpuAdmission104 = scheduler.submit([&, index]()noexcept{ order[cursor.fetch_add(1u)] = index; }, options);
        EXPECT_TRUE(cpuAdmission104);
        if(cpuAdmission104)
            EXPECT_TRUE(cpuAdmission104->valid());
    }
    occupied.open();
    scheduler.wait();
    EXPECT_FALSE(occupied.expired.load());
    EXPECT_EQ(cursor.load(), 3u);
    EXPECT_EQ(order[0u], s_ExpectedDualCount);
    EXPECT_EQ(order[1u], 1u);
    EXPECT_EQ(order[s_ThirdElementIndex], 0u);
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsQueuedBodiesAndTheirDependentsAndRetiresCaptures){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    Atomic<u32> retired{ 0u };
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invoked = 0u;
    const auto cpuAdmission105 = scope.submit([&invoked, capture = LifetimeProbe(retired, 1u)](){ invoked += capture.value; });
    ASSERT_TRUE(cpuAdmission105);
    const auto predecessor = *cpuAdmission105;
    const auto cpuAdmission106 = scheduler.submit([&invoked, capture = LifetimeProbe(retired, s_ExpectedDualCount)](){ invoked += capture.value; }, predecessor);
    ASSERT_TRUE(cpuAdmission106);
    const auto dependent = *cpuAdmission106;
    scope.cancel();
    const auto cpuAdmission107 = scope.submit([&]()noexcept{ ++invoked; });
    EXPECT_FALSE(cpuAdmission107);
    scheduler.wait(dependent);
    scope.wait();
    EXPECT_EQ(invoked, 0u);
    EXPECT_EQ(retired.load(), s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().canceledTasks, s_ExpectedDualCount);
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
    const auto cpuAdmission108 = scope.submit([&](){
        parentGate.block();
        const auto cpuAdmission109 = scheduler.submit([&](){ childInvocations.fetch_add(1u); });
        if(!cpuAdmission109 || !cpuAdmission109->valid())
            childRejected.store(true, MemoryOrder::release);
    }
    );
    ASSERT_TRUE(cpuAdmission108);
    const auto parent = *cpuAdmission108;
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(parentGate.entered, s_GateTimeoutMS));
    const auto cpuAdmission110 = scheduler.submit([&]()noexcept{ continuationInvocations.fetch_add(1u); }, parent);
    ASSERT_TRUE(cpuAdmission110);
    const auto continuation = *cpuAdmission110;
    scope.cancel();
    parentGate.open();
    scheduler.wait(continuation);
    scope.wait();
    EXPECT_FALSE(parentGate.expired.load());
    EXPECT_EQ(childInvocations.load(), 0u);
    EXPECT_EQ(continuationInvocations.load(), 0u);
    EXPECT_GE(scheduler.statistics().canceledTasks, s_ExpectedDualCount);
    EXPECT_EQ(scheduler.statistics().outstandingTasks, 0u);
}


TEST(CpuTaskSchedulerTests, HeavyWorkUsesTheOtherClassWhileItsPreferredWorkerIsOccupied){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    TaskGate preferredGate;
    TaskGate spillGate;
    CpuTaskScheduler scheduler(s_ExpectedDualCount);
    ScopeExit releaseWorkers([&]()noexcept{ preferredGate.open(); spillGate.open(); });
    const auto workers = scheduler.statistics();
    if(workers.performanceWorkers == 0u || workers.efficiencyWorkers == 0u)
        GTEST_SKIP() << "The permitted CPUs do not provide both capacity classes.";
    CpuAffinity::Enum preferredAffinity = CpuAffinity::Any;
    CpuAffinity::Enum spillAffinity = CpuAffinity::Any;
    const auto cpuAdmission111 = scheduler.submit([&](){
        preferredAffinity = scheduler.currentWorkerAffinity();
        preferredGate.block();
    }
    );
    EXPECT_TRUE(cpuAdmission111);
    if(cpuAdmission111)
        EXPECT_TRUE(cpuAdmission111->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(preferredGate.entered, s_GateTimeoutMS));
    const auto cpuAdmission112 = scheduler.submit([&](){
        spillAffinity = scheduler.currentWorkerAffinity();
        spillGate.block();
    }
    );
    EXPECT_TRUE(cpuAdmission112);
    if(cpuAdmission112)
        EXPECT_TRUE(cpuAdmission112->valid());
    EXPECT_TRUE(CpuTaskTestSupport::WaitUntil(spillGate.entered, s_GateTimeoutMS));
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
        const auto cpuAdmission113 = scheduler.submit([&](){ start.block(); scheduler.wait(self); });
        if(!cpuAdmission113)
            ExitTestProcess(0u);
        self = *cpuAdmission113;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantWaitForAnAncestorIsRejectedInsteadOfDeadlocking){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle parent;
        const auto cpuAdmission114 = scheduler.submit([&](){
            start.block();
            const auto cpuAdmission115 = scheduler.submit([&](){ scheduler.wait(parent); });
            if(!cpuAdmission115)
                ExitTestProcess(0u);
            const auto child = *cpuAdmission115;
            EXPECT_TRUE(child.valid());
        }
        );
        if(!cpuAdmission114)
            ExitTestProcess(0u);
        parent = *cpuAdmission114;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, JoiningTheScopeThatContainsTheCurrentTaskIsRejected){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskScope scope(scheduler);
        const auto cpuAdmission116 = scope.submit([&](){ scope.wait(); });
        EXPECT_TRUE(cpuAdmission116);
        if(cpuAdmission116)
            EXPECT_TRUE(cpuAdmission116->valid());
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, CanceledScopeSkipsBothDirectParallelRangeOverloads){
    using namespace __hidden_cpu_task_scheduler_tests;
    DeadlineGuard deadline;
    CpuTaskScheduler scheduler(0u);
    CpuTaskScope scope(scheduler);
    u32 invocations = 0u;
    scope.cancel();
    EXPECT_TRUE((scope.parallelFor(0u, 17u, [&](usize)noexcept{ ++invocations; })));
    EXPECT_TRUE((scope.parallelFor(4u, 25u, 3u, [&](usize)noexcept{ ++invocations; })));
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
    EXPECT_TRUE((scope.parallelFor(0u, 64u, 1u, [&](usize index){
        ++invocations;
        if(index == 0u)
            scope.cancel();
    }
    )));
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
    const auto cpuAdmission117 = canceledScope.submit([&]()noexcept{ ++canceledInvocations; });
    ASSERT_TRUE(cpuAdmission117);
    const auto canceled = *cpuAdmission117;
    canceledScope.cancel();
    canceledScope.wait();
    for(u32 iteration = 0u; iteration < 16u; ++iteration){
        const auto cpuAdmission118 = scheduler.submit([&]()noexcept{ ++unrelatedInvocations; });
        ASSERT_TRUE(cpuAdmission118);
        const auto unrelated = *cpuAdmission118;
        scheduler.wait(unrelated);
    }
    const auto cpuAdmission119 = scheduler.submit([&]()noexcept{ ++canceledInvocations; }, canceled);
    ASSERT_TRUE(cpuAdmission119);
    const auto lateDependent = *cpuAdmission119;
    ASSERT_TRUE(lateDependent.valid());
    scheduler.wait(lateDependent);
    const auto cpuAdmission120 = scheduler.submit([&]()noexcept{ ++canceledInvocations; }, lateDependent);
    ASSERT_TRUE(cpuAdmission120);
    const auto laterDependent = *cpuAdmission120;
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
        const auto cpuAdmission121 = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, parent);
            rejected = !child;
        }
        );
        if(!cpuAdmission121)
            ExitTestProcess(2u);
        parent = *cpuAdmission121;
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(parent);
        ExitTestProcess(scheduler.statistics().outstandingTasks == 0u ? 0u : s_ExpectedDualCount);
    }, testing::ExitedWithCode(0), ""
    );
}


TEST(CpuTaskSchedulerTests, ChildCannotDependOnATransitiveDependentOfItsStructuredParent){
    EXPECT_EXIT({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline;
        CpuTaskScheduler scheduler(0u);
        CpuTaskHandle dependent;
        bool rejected = false;
        u32 continuations = 0u;
        const auto cpuAdmission123 = scheduler.submit([&](){
            const auto child = scheduler.submit([]()noexcept{}, dependent);
            rejected = !child;
        }
        );
        if(!cpuAdmission123)
            ExitTestProcess(2u);
        const auto parent = *cpuAdmission123;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto cpuAdmission125 = scheduler.submit([&]()noexcept{ ++continuations; }, dependent);
            if(!cpuAdmission125)
                ExitTestProcess(2u);
            dependent = *cpuAdmission125;
        }
        scheduler.pumpMainThread();
        if(!rejected)
            ExitTestProcess(1u);
        scheduler.wait(dependent);
        ExitTestProcess(continuations == 3u && scheduler.statistics().outstandingTasks == 0u ? 0u : s_ExpectedDualCount);
    }, testing::ExitedWithCode(0), ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantOnAnotherWorkerCannotJoinItsAncestorsScope){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate parentGate;
        CpuTaskScheduler scheduler(HomogeneousWorkers(s_ExpectedDualCount));
        CpuTaskScope scope(scheduler);
        Atomic<usize> parentWorker{ 0u };
        const auto cpuAdmission126 = scope.submit([&](){
            parentWorker.store(scheduler.currentWorkerIndex(), MemoryOrder::release);
            const auto cpuAdmission127 = scheduler.submit([&](){
                if(!CpuTaskTestSupport::WaitUntil(parentGate.entered, s_GateTimeoutMS) || scheduler.currentWorkerIndex() == parentWorker.load(MemoryOrder::acquire))
                    ExitTestProcess(0u);
                scope.wait();
            }
            );
            EXPECT_TRUE(cpuAdmission127);
            if(cpuAdmission127)
                EXPECT_TRUE(cpuAdmission127->valid());
            parentGate.block();
        }
        );
        EXPECT_TRUE(cpuAdmission126);
        if(cpuAdmission126)
            EXPECT_TRUE(cpuAdmission126->valid());
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, WorkerCannotWaitForAnIndependentlySubmittedDependent){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto cpuAdmission128 = scheduler.submit([&](){ start.block(); scheduler.wait(dependent); });
        if(!cpuAdmission128)
            ExitTestProcess(0u);
        const auto running = *cpuAdmission128;
        const auto cpuAdmission129 = scheduler.submit([]()noexcept{}, running);
        if(!cpuAdmission129)
            ExitTestProcess(0u);
        dependent = *cpuAdmission129;
        start.open();
        scheduler.wait();
    }, ""
    );
}


TEST(CpuTaskSchedulerTests, DescendantCannotWaitForTransitiveDependentsOfItsAncestor){
    EXPECT_DEATH({
        using namespace __hidden_cpu_task_scheduler_tests;
        DeadlineGuard deadline(true);
        TaskGate start;
        CpuTaskScheduler scheduler(HomogeneousWorkers(1u));
        CpuTaskHandle dependent;
        const auto cpuAdmission130 = scheduler.submit([&](){
            start.block();
            const auto cpuAdmission131 = scheduler.submit([&](){ scheduler.wait(dependent); });
            EXPECT_TRUE(cpuAdmission131);
            if(cpuAdmission131)
                EXPECT_TRUE(cpuAdmission131->valid());
        }
        );
        if(!cpuAdmission130)
            ExitTestProcess(0u);
        const auto parent = *cpuAdmission130;
        dependent = parent;
        for(u32 depth = 0u; depth < 3u; ++depth){
            const auto cpuAdmission132 = scheduler.submit([]()noexcept{}, dependent);
            if(!cpuAdmission132)
                ExitTestProcess(0u);
            dependent = *cpuAdmission132;
        }
        start.open();
        scheduler.wait();
    }, ""
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

