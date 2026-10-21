// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cpu_task_scheduler_test_support.h"

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cpu_task_scheduler_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;

using namespace CpuTaskSchedulerTestSupport;


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
    const auto cpuAdmission81 = scheduler.submit([&]()noexcept{ rootGate.block(); });
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
            const auto cpuAdmission91 = scheduler.submit([&grandchildGate, &total, &parentCapture, &childCapture, grandchildCapture = LifetimeProbe(retirements, 17u)]()noexcept{
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
    const auto cpuAdmission93 = unrelated.submit([&]()noexcept{ unrelatedGate.block(); });
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
            const auto cpuAdmission96 = scheduler.submit([&invoked, capture = LifetimeProbe(retired, 23u)]()noexcept{
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
        const auto cpuAdmission99 = scheduler.submit([&]()noexcept{
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
        const auto cpuAdmission102 = scheduler.submit([&]()noexcept{ value *= 3u; actual = QueryCurrentThreadId(); }, mainOptions, &child, 1u);
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

